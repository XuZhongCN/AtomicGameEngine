//
// Copyright (c) 2014-2016, THUNDERBEAST GAMES LLC All rights reserved
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "Precompiled.h"
#include "../Core/Context.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/GraphicsEvents.h"
#include "../Graphics/View.h"
#include "../Scene/Scene.h"
#include "../Graphics/Camera.h"
#include "../Scene/SceneEvents.h"
#include "../Graphics/Light.h"
#include "../Resource/ResourceCache.h"
#include "../Graphics/Technique.h"
#include "../Environment/FoliageSystem.h"
#include "../Graphics/Renderer.h"
#include <Atomic/Math/Vector3.h>
#include <Atomic/IO/Log.h>
#if defined(_MSC_VER)
#include "stdint.h"
#endif

#if defined(EMSCRIPTEN) || defined(ATOMIC_PLATFORM_LINUX)
#include <stdint.h>
#endif

namespace Atomic
{

	extern const char* GEOMETRY_CATEGORY;

	FoliageSystem::FoliageSystem(Context *context) : Component(context)
	{
		initialized_ = false;
		context_ = context;
	}

	FoliageSystem::~FoliageSystem()
	{
	}

	//void FoliageSystem::ApplyAttributes()
	//{
	
	//}



	void FoliageSystem::RegisterObject(Context* context)
	{
		context->RegisterFactory<FoliageSystem>(GEOMETRY_CATEGORY);
		ATOMIC_ACCESSOR_ATTRIBUTE("Is Enabled", IsEnabled, SetEnabled, bool, true, AM_DEFAULT);
	}


	void FoliageSystem::HandleComponentRemoved(StringHash eventType, VariantMap& eventData)
	{
		Component* component = static_cast<Component*> (eventData[Atomic::ComponentRemoved::P_COMPONENT].GetPtr());
		if (component == this) {
			for (HashMap<IntVector2, GeomReplicator*>::Iterator i = vegReplicators_.Begin(); i != vegReplicators_.End(); ++i)
			{
				i->second_->Remove();
			}
		}

	}


	void FoliageSystem::Initialize()
	{
		initialized_ = true;
		SubscribeToEvent(node_->GetScene(), E_COMPONENTREMOVED, ATOMIC_HANDLER(FoliageSystem, HandleComponentRemoved));
	}

	void FoliageSystem::OnSetEnabled()
	{
		bool enabled = IsEnabledEffective();

		for (HashMap<IntVector2, GeomReplicator*>::Iterator i = vegReplicators_.Begin(); i != vegReplicators_.End(); ++i)
		{
			i->second_->SetEnabled(false);
		}

	}



	void FoliageSystem::OnNodeSet(Node* node)
	{
		if (node && !initialized_)
		{
			node_ = node;
			node->AddListener(this);

			PODVector<Terrain*> terrains;
			node->GetDerivedComponents<Terrain>(terrains);

			if (terrains.Size() > 0)
			{
				terrain_ = terrains[0];
				SubscribeToEvent(node->GetScene(), E_SCENEDRAWABLEUPDATEFINISHED, ATOMIC_HANDLER(FoliageSystem, HandleDrawableUpdateFinished));
				SubscribeToEvent(E_BEGINFRAME, ATOMIC_HANDLER(FoliageSystem, HandlePostUpdate));
				// TODO: Make this better
				// If we try to get height of the terrain right away it will be zero because it's not finished loading. So I wait until the scene has finished
				// updating all its drawables (for want of a better event) and then initialize the grass if it isn't already initialized.
			}
		}
	}
	void FoliageSystem::HandleDrawableUpdateFinished(StringHash eventType, VariantMap& eventData)
	{
		if (!initialized_)
			Initialize();
		this->UnsubscribeFromEvent(E_SCENEDRAWABLEUPDATEFINISHED);
		
	}

	void FoliageSystem::HandlePostUpdate(StringHash eventType, VariantMap& eventData)
	{
		if (!initialized_)
			return;

		Renderer* r = GetSubsystem<Renderer>();
		if (!r)
			return;

		Viewport* viewport = r->GetViewport(0);
		if (!viewport)
			return;

		IntVector2 terrainsize = (terrain_->GetNumPatches() * terrain_->GetPatchSize());
		IntVector2 cellsize = terrainsize / 16;

		Camera *cam =  viewport->GetCamera();
		if (cam) {
			Vector3 campos = cam->GetNode()->GetPosition();
			campos.y_ = 0;

			IntVector2 campos2d = IntVector2(campos.x_, campos.z_);


			IntVector2 sector = IntVector2(  floor(campos2d.x_ / cellsize.x_), floor(campos2d.y_ / cellsize.y_));
		

			//ATOMIC_LOGDEBUG(sector.ToString());
			Vector3 pos = Vector3((sector.x_ * cellsize.x_) - cellsize.x_, 0, (sector.y_ * cellsize.y_) - cellsize.y_);
			
			if (sectorSet_ && lastSector_ != sector)
			{

				for (HashMap<IntVector2, GeomReplicator*>::Iterator i = vegReplicators_.Begin(); i != vegReplicators_.End(); ++i) {
						i->second_->Remove();
						vegReplicators_.Erase(i->first_);
				}

				sectorSet_ = true;
				lastSector_ = sector;

				ATOMIC_LOGDEBUG("New grass " + pos.ToString() + " Sector: " + sector.ToString());
				
				DrawGrass(pos, sector, cellsize);
	/*			DrawGrass(pos, sector + IntVector2(1,1), cellsize);
				DrawGrass(pos, sector + IntVector2(1, -1), cellsize);
				DrawGrass(pos, sector + IntVector2(-1, 1), cellsize);
				DrawGrass(pos, sector + IntVector2(-1, -1));*/
			}
		}
	}

	void FoliageSystem::DrawGrass(Vector3 position, IntVector2 sector, IntVector2 cellsize) {
		const unsigned NUM_OBJECTS = 1000;

		if (!terrain_){
			ATOMIC_LOGERROR("Foliage system couldn't find terrain");
			return;
		}

		ResourceCache* cache = GetSubsystem<ResourceCache>();
		PODVector<PRotScale> qpList_;
	//	Vector3 rotatedpos = (rot.Inverse() * qp.pos);  //  (rot.Inverse() * qp.pos) + terrainpos;
		for (unsigned i = 0; i < NUM_OBJECTS; ++i)
		{
			PRotScale qp;
			

			qp.pos = (node_->GetRotation().Inverse() * Vector3(Random(cellsize.x_), 0.0f, Random(cellsize.y_))) + (node_->GetRotation().Inverse() * position);
			qp.rot = Quaternion(0.0f, Random(360.0f), 0.0f);
			qp.pos.y_ = terrain_->GetHeight(node_->GetRotation() * qp.pos) - 0.2f;
			qp.scale = 2.5f + Random(2.0f);
			qpList_.Push(qp);
		}

		Model *pModel = cache->GetResource<Model>("Models/Veg/vegbrush.mdl");
		SharedPtr<Model> cloneModel = pModel->Clone();


		Node *grassnode = node_->CreateChild();
		GeomReplicator *grass = grassnode->CreateComponent<GeomReplicator>();
		grass->SetModel(cloneModel);
		grass->SetMaterial(cache->GetResource<Material>("Models/Veg/veg-alphamask.xml"));

		Vector3 lightDir(0.6f, -1.0f, 0.8f);

		lightDir = -1.0f * lightDir.Normalized();
		grass->Replicate(qpList_, lightDir);

		// specify which verts in the geom to move
		// - for the vegbrush model, the top two vertex indeces are 2 and 3
		PODVector<unsigned> topVerts;
		topVerts.Push(2);
		topVerts.Push(3);

		// specify the number of geoms to update at a time
		unsigned batchCount = 10000;

		// wind velocity (breeze velocity shown)
		Vector3 windVel(0.1f, -0.1f, 0.1f);

		// specify the cycle timer
		float cycleTimer = 1.4f;

		grass->ConfigWindVelocity(topVerts, batchCount, windVel, cycleTimer);
		grass->WindAnimationEnabled(true);

		vegReplicators_.InsertNew(sector, grass);

	}

}
