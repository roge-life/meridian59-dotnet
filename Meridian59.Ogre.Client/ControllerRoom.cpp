#include "stdafx.h"

namespace Meridian59 { namespace Ogre 
{
   static ControllerRoom::ControllerRoom()
   {
      roomDecoration          = nullptr;
      roomNode                = nullptr;
      roomManObj              = nullptr;
      weatherNode             = nullptr;
      grassMaterials          = nullptr;
      grassPoints             = nullptr;
      waterTextures           = nullptr;
      caelumSystem            = nullptr;
      avatarObject            = nullptr;
      particleSysSnow         = nullptr;
      particleSysRain         = nullptr;
      customParticleHandlers  = nullptr;
      recreatequeue           = nullptr;
      verticesProcessed       = 0;
   };

   RooFile^ ControllerRoom::Room::get()
   {
      return OgreClient::Singleton->Data->RoomInformation->ResourceRoom;
   };

   ::Ogre::SceneManager* ControllerRoom::SceneManager::get()
   {
      return OgreClient::Singleton->SceneManager;
   };

   void ControllerRoom::Initialize()
   {
      if (IsInitialized)
         return;

      // init collections
      grassMaterials = gcnew ::System::Collections::Generic::Dictionary<unsigned short, array<CLRString^>^>();
      grassPoints    = gcnew ::System::Collections::Generic::Dictionary<CLRString^, ::System::Collections::Generic::List<V3>^>();
      waterTextures  = gcnew ::System::Collections::Generic::List<CLRString^>();

      // create the queue storing materialnames (chunks of the room) which will be recreated
      // at the end of the tick
      recreatequeue = gcnew ::System::Collections::Generic::List<CLRString^>();

      // a manualobject for the room geometry
      roomManObj = OGRE_NEW ManualObject(NAME_ROOM);
      roomManObj->setDynamic(true);
      roomManObj->setRenderQueueGroup(RENDER_QUEUE_WORLD_GEOMETRY_1);

      // a manualobject for the room decoration
      roomDecoration = OGRE_NEW ManualObject(NAME_ROOMDECORATION);
      roomDecoration->setDynamic(false);
      roomDecoration->setRenderQueueGroup(RENDER_QUEUE_WORLD_GEOMETRY_1);

      // create room scenenode
      roomNode = SceneManager->getRootSceneNode()->createChildSceneNode(NAME_ROOMNODE);
      roomNode->setPosition(::Ogre::Vector3(64.0f, 0, 64.0f));
      if (roomManObj->isAttached()) { 
        roomManObj->detachFromParent();
         }
        roomNode->attachObject(roomManObj);

      if (roomDecoration->isAttached()) { 
        roomDecoration->detachFromParent();
         }
        roomNode->attachObject(roomDecoration);
      roomNode->setInitialState();

      // create rootnode for weather effects
      weatherNode = OgreClient::Singleton->CameraNodeOrbit->createChildSceneNode(NAME_WEATHERNODE);
      weatherNode->setPosition(::Ogre::Vector3(0, 0, 0));
      weatherNode->setInitialState();

      // create decoration mapping
      LoadImproveData();

      // init caelum
      InitCaelum();

      // init room based particle systems
      InitParticleSystems();

      /******************************************************************/

      // projectiles listener
         OgreClient::Singleton->Data->Projectiles->ListChanged += 
         gcnew ListChangedEventHandler(&ControllerRoom::OnProjectilesListChanged);
            
      // roomobjects listener
         OgreClient::Singleton->Data->RoomObjects->ListChanged += 
         gcnew ListChangedEventHandler(&ControllerRoom::OnRoomObjectsListChanged);
        
      // camera-position listener
      OgreClient::Singleton->Data->PropertyChanged += 
         gcnew PropertyChangedEventHandler(&ControllerRoom::OnDataPropertyChanged);

      // effects listeners
      OgreClient::Singleton->Data->Effects->Snowing->PropertyChanged +=
         gcnew PropertyChangedEventHandler(&ControllerRoom::OnEffectSnowingPropertyChanged);
      OgreClient::Singleton->Data->Effects->Raining->PropertyChanged +=
         gcnew PropertyChangedEventHandler(&ControllerRoom::OnEffectRainingPropertyChanged);

      /******************************************************************/

      // add existing objects to scene
      for each (RoomObject^ roomObject in OgreClient::Singleton->Data->RoomObjects)
         RoomObjectAdd(roomObject);

      // add existing projectiles to scene
      for each (Projectile^ projectile in OgreClient::Singleton->Data->Projectiles)
         ProjectileAdd(projectile);

      /******************************************************************/

      IsInitialized = true;
   };

   void ControllerRoom::InitCaelum()
   {
      // don't init twice
      if (caelumSystem)
         return;

      /**************************** 1. INIT *******************************************************/

      // configuration flags for caelum (either full or sun/moon only)
      ::Caelum::CaelumSystem::CaelumComponent flags = (OgreClient::Singleton->Config->DisableNewSky) ?
         (::Caelum::CaelumSystem::CaelumComponent)(
            ::Caelum::CaelumSystem::CaelumComponent::CAELUM_COMPONENT_MOON |
            ::Caelum::CaelumSystem::CaelumComponent::CAELUM_COMPONENT_SUN) :
         (::Caelum::CaelumSystem::CaelumComponent)(
            ::Caelum::CaelumSystem::CaelumComponent::CAELUM_COMPONENT_SKY_DOME |
            ::Caelum::CaelumSystem::CaelumComponent::CAELUM_COMPONENT_MOON |
            ::Caelum::CaelumSystem::CaelumComponent::CAELUM_COMPONENT_SUN |
            ::Caelum::CaelumSystem::CaelumComponent::CAELUM_COMPONENT_POINT_STARFIELD |
            ::Caelum::CaelumSystem::CaelumComponent::CAELUM_COMPONENT_CLOUDS);

      // init caelumsystem
      caelumSystem = new ::Caelum::CaelumSystem(
         OgreClient::Singleton->Root,
         SceneManager,
         flags);

      // don't manage ambientlight, use only 1 directional light and no fog
      caelumSystem->setManageAmbientLight(false);
      caelumSystem->setEnsureSingleLightSource(true);
      caelumSystem->setManageSceneFog(::Ogre::FogMode::FOG_NONE);
      //CaelumSystem->setSceneFogDensityMultiplier(0.0f);

      // attach viewport
      caelumSystem->attachViewport(OgreClient::Singleton->Viewport);
      //CaelumSystem->attachViewport(OgreClient::Singleton->ViewportInvis);

      // hookup listeners
      //OgreClient::Singleton->Root->addFrameListener(CaelumSystem);
      //OgreClient::Singleton->RenderWindow->addListener(CaelumSystem);

      /**************************** 2. TIME/DAY DURATION *******************************************/

      const int YEAR = 1;
      const int MONTH = 4;
      const int DAY = 1;

      // get caelum clock instance and current m59 time
      ::Caelum::UniversalClock* clock = caelumSystem->getUniversalClock();
      ::System::DateTime time = MeridianDate::GetMeridianTime();

      // set caelum day duration to m59 day duration
      // and set current time using dummy date
      clock->setTimeScale((float)MeridianDate::M59SECONDSPERSECOND);
      clock->setGregorianDateTime(YEAR, MONTH, DAY, time.Hour, time.Minute, time.Second);

      /**************************** 3. CLOUDS ******************************************************/

      ::Caelum::CloudSystem* clouds = caelumSystem->getCloudSystem();

      if (clouds)
      {
         clouds->createLayerAtHeight(2000.0f);
         clouds->createLayerAtHeight(5000.0f);
         clouds->getLayer(0)->setCloudSpeed(Ogre::Vector2(0.00010f, -0.00018f));
         clouds->getLayer(1)->setCloudSpeed(Ogre::Vector2(0.00009f, -0.00017f));
      }

      /**************************** 4. LIGHT ******************************************************/

      AdjustAmbientLight();
   };

   void ControllerRoom::InitParticleSystems()
   {
      // don't init twice or if disabled
      if (IsInitialized || OgreClient::Singleton->Config->DisableWeatherEffects)
         return;

      customParticleHandlers = new ::std::vector<::ParticleUniverse::ParticleEventHandler*>();

      ::ParticleUniverse::ParticleSystemManager* particleMan =
         ::ParticleUniverse::ParticleSystemManager::getSingletonPtr();

      // create room based particle systems
      particleSysSnow = particleMan->createParticleSystem(
         PARTICLES_SNOW_NAME, PARTICLES_SNOW_TEMPLATE, SceneManager);

      particleSysRain = particleMan->createParticleSystem(
         PARTICLES_RAIN_NAME, PARTICLES_RAIN_TEMPLATE, SceneManager);

      // setup particle system: snow
      if (particleSysSnow->getNumTechniques() > 0)
      {
         ::ParticleUniverse::ParticleTechnique* technique =
            particleSysSnow->getTechnique(0);

         if (technique->getNumObservers() > 0)
         {
            ::ParticleUniverse::OnPositionObserver* observer = (::ParticleUniverse::OnPositionObserver*)
               technique->getObserver(0);

            if (observer)
            {
               // create custom handler for OnPosition
               // this will track the position and adjust
               WeatherParticleEventHandler* posHandler = 
                  new WeatherParticleEventHandler();

               observer->addEventHandler(
                  (::ParticleUniverse::ParticleEventHandler*)posHandler);

               // save reference for cleanup
               customParticleHandlers->push_back(
                  (::ParticleUniverse::ParticleEventHandler*)posHandler);
            }
         }

         // set particles count from config
         technique->setVisualParticleQuota(OgreClient::Singleton->Config->WeatherParticles);

         // adjust emission rate to 1/10 of max quota
         if (technique->getNumEmitters() > 0)
         {
            ::ParticleUniverse::DynamicAttributeFixed* val = (::ParticleUniverse::DynamicAttributeFixed*)
               technique->getEmitter(0)->getDynEmissionRate();

            val->setValue((::ParticleUniverse::Real)(OgreClient::Singleton->Config->WeatherParticles / 10));
         }
      }

      // setup particle system: rain
      if (particleSysRain->getNumTechniques() > 0)
      {
         ::ParticleUniverse::ParticleTechnique* technique =
            particleSysRain->getTechnique(0);

         if (technique->getNumObservers() > 0)
         {
            ::ParticleUniverse::OnPositionObserver* observer = (::ParticleUniverse::OnPositionObserver*)
               technique->getObserver(0);

            if (observer)
            {
               // create custom handler for OnPosition
               // this will track the position and adjust
               WeatherParticleEventHandler* posHandler =
                  new WeatherParticleEventHandler();

               observer->addEventHandler(
                  (::ParticleUniverse::ParticleEventHandler*)posHandler);

               // save reference for cleanup
               customParticleHandlers->push_back(
                  (::ParticleUniverse::ParticleEventHandler*)posHandler);
            }
         }

         // set particles count from config
         technique->setVisualParticleQuota(OgreClient::Singleton->Config->WeatherParticles);

         // adjust emission rate to 1/10 of max quota
         if (technique->getNumEmitters() > 0)
         {
            ::ParticleUniverse::DynamicAttributeFixed* val = (::ParticleUniverse::DynamicAttributeFixed*)
               technique->getEmitter(0)->getDynEmissionRate();

            val->setValue((::ParticleUniverse::Real)(OgreClient::Singleton->Config->WeatherParticles / 5));
         }
      }
   };

   void ControllerRoom::DestroyParticleSystems()
   {
      if (!IsInitialized)
         return;

      ::ParticleUniverse::ParticleSystemManager* particleMan =
         ::ParticleUniverse::ParticleSystemManager::getSingletonPtr();

      if (particleSysSnow)
         particleMan->destroyParticleSystem(particleSysSnow, SceneManager);

      if (particleSysRain)
         particleMan->destroyParticleSystem(particleSysRain, SceneManager);

      if (customParticleHandlers)
      {
         // free custom event handler allocations
         for(size_t i = 0; i < customParticleHandlers->size(); i++)
            delete customParticleHandlers->at(i);

         // clear custom particle handler list
         customParticleHandlers->clear();

         delete customParticleHandlers;
      }

      particleSysSnow = nullptr;
      particleSysRain = nullptr;
   };

   void ControllerRoom::DestroyCaelum()
   {
      if (!caelumSystem)
         return;

      caelumSystem->detachViewport(OgreClient::Singleton->Viewport);

      //OgreClient::Singleton->Root->removeFrameListener(CaelumSystem);
      //OgreClient::Singleton->RenderWindow->removeListener(CaelumSystem);

      caelumSystem->shutdown(true);
      caelumSystem = NULL;
   };

   void ControllerRoom::Destroy()
   {
      if (!IsInitialized)
         return;

      UnloadRoom();
      DestroyCaelum();
      DestroyParticleSystems();

      /******************************************************************/

      // remove listener from projectiles
      OgreClient::Singleton->Data->Projectiles->ListChanged -= 
         gcnew ListChangedEventHandler(&OnProjectilesListChanged);

      // remove listener from roomobjects
      OgreClient::Singleton->Data->RoomObjects->ListChanged -= 
         gcnew ListChangedEventHandler(&OnRoomObjectsListChanged);

      // remove listener
      OgreClient::Singleton->Data->PropertyChanged -= 
         gcnew PropertyChangedEventHandler(&OnDataPropertyChanged);

      // remove effects listeners
      OgreClient::Singleton->Data->Effects->Snowing->PropertyChanged -=
         gcnew PropertyChangedEventHandler(&OnEffectSnowingPropertyChanged);
      OgreClient::Singleton->Data->Effects->Raining->PropertyChanged -=
         gcnew PropertyChangedEventHandler(&OnEffectRainingPropertyChanged);

      /******************************************************************/

      if (SceneManager->hasSceneNode(NAME_ROOMNODE))
         SceneManager->destroySceneNode(NAME_ROOMNODE);

      if (SceneManager->hasSceneNode(NAME_WEATHERNODE))
         SceneManager->destroySceneNode(NAME_WEATHERNODE);

      if (SceneManager->hasManualObject(NAME_ROOMDECORATION))
         SceneManager->destroyManualObject(NAME_ROOMDECORATION);

      if (SceneManager->hasManualObject(NAME_ROOM))
         SceneManager->destroyManualObject(NAME_ROOM);

      /******************************************************************/

      delete grassMaterials;
      delete grassPoints;
      delete recreatequeue;
      delete waterTextures;

      /******************************************************************/

      roomDecoration    = nullptr;
      roomNode          = nullptr;
      roomManObj        = nullptr;
      weatherNode       = nullptr;
      caelumSystem      = nullptr;
      grassMaterials    = nullptr;
      grassPoints       = nullptr;
      waterTextures     = nullptr;
      avatarObject      = nullptr;
      recreatequeue     = nullptr;
      verticesProcessed = 0;

      /******************************************************************/
      IsInitialized = false;
   };

   void ControllerRoom::LoadRoom()
   {
      double tick1, tick2, span;

      /*********************************************************************************************/

      // roomfile must be present
      if (!Room)
      {
         // log
         Logger::Log(MODULENAME, LogType::Error,
            "Error: Room (.roo) resource not attached to RoomInformation.");

         return;
      }

      /*********************************************************************************************/

      // attach handlers for changes in the room
      Room->WallTextureChanged   += gcnew WallTextureChangedEventHandler(OnRooFileWallTextureChanged);
      Room->SectorTextureChanged += gcnew SectorTextureChangedEventHandler(OnRooFileSectorTextureChanged);
      Room->SectorMoved          += gcnew SectorMovedEventHandler(OnRooFileSectorMoved);

      /*********************************************************************************************/

      // adjust octree
      AdjustOctree();

      // adjust ambient light       
      AdjustAmbientLight();

      // set sky
      UpdateSky();

      // get materialinfos
      ::System::Collections::Generic::Dictionary<CLRString^, RooFile::MaterialInfo>^ dict =
         Room->GetMaterialInfos();

      Logger::Log(MODULENAME, LogType::Info, "Start loading room: " + Room->Filename + FileExtensions::ROO);

      /*********************************************************************************************/
      /*                              MINIMAP WALLS                                                */
      /*********************************************************************************************/
      MiniMapCEGUI::SetMapData(Room->Walls);

      /*********************************************************************************************/
      /*                              ROOM TEXTURES                                                */
      /*********************************************************************************************/

      tick1 = OgreClient::Singleton->GameTick->GetUpdatedTick();

      // create the materials & textures
      for each(KeyValuePair<CLRString^, RooFile::MaterialInfo> pair in dict)
      {
         // create texture & material
         CreateTextureAndMaterial(
            pair.Value.Texture,
            pair.Value.TextureName,
            pair.Value.MaterialName,
            pair.Value.ScrollSpeed);
      }

      tick2 = OgreClient::Singleton->GameTick->GetUpdatedTick();
      span = tick2 - tick1;

      Logger::Log(MODULENAME, LogType::Info, "Textures: " + span.ToString() + " ms");

      /*********************************************************************************************/
      /*                              ROOM GEOMETRY                                                */
      /*********************************************************************************************/

      tick1 = tick2;

      // create room geometry
      for each(KeyValuePair<CLRString^, RooFile::MaterialInfo> pair in dict)
         CreateGeometryChunk(pair.Value.MaterialName);

      tick2 = OgreClient::Singleton->GameTick->GetUpdatedTick();
      span = tick2 - tick1;

      Logger::Log(MODULENAME, LogType::Info, "Geometry: " + span.ToString() + " ms");

      /*********************************************************************************************/
      /*                               ROOM DECORATION                                             */
      /*********************************************************************************************/

      tick1 = tick2;

      // create room decoration
      CreateDecoration();

      tick2 = OgreClient::Singleton->GameTick->GetUpdatedTick();
      span = tick2 - tick1;

      Logger::Log(MODULENAME, LogType::Info, "Decoration: " + span.ToString() + " ms");

      /*********************************************************************************************/
      /*                                    OTHERS                                                 */
      /*********************************************************************************************/

   };

   void ControllerRoom::UnloadRoom()
   {
      // stop all particle systems
      if (particleSysSnow)
      {
         particleSysSnow->stop();

         if (particleSysSnow->isAttached())
            particleSysSnow->detachFromParent();
      }
      if (particleSysRain)
      {
         particleSysRain->stop();

         if (particleSysRain->isAttached())
            particleSysRain->detachFromParent();
      }

      // childnodes/room elements
      if (roomNode)
         roomNode->removeAllChildren();

         // clear room decoration
      if (roomDecoration)
         roomDecoration->clear();

      // clear room geometry
      if (roomManObj)
         roomManObj->clear();

      if (grassPoints)
         grassPoints->Clear();

      if (Room)
      {
         // detach listeners
         Room->WallTextureChanged   -= gcnew WallTextureChangedEventHandler(OnRooFileWallTextureChanged);
         Room->SectorTextureChanged -= gcnew SectorTextureChangedEventHandler(OnRooFileSectorTextureChanged);
         Room->SectorMoved          -= gcnew SectorMovedEventHandler(OnRooFileSectorMoved);
      }
   };

   int ControllerRoom::GetRoomSectionByMaterial(const ::Ogre::String& Name)
   {
      ::Ogre::ManualObject::ManualObjectSection* section;

      if (!roomManObj || Name == STRINGEMPTY)
         return -1;

      for (unsigned int i = 0; i < roomManObj->getNumSections(); i++)
      {
         section = roomManObj->getSection(i);

         if (section->getMaterialName() == Name)
            return (int)i;
      }

      return -1;
   };

   int ControllerRoom::GetDecorationSectionByMaterial(const ::Ogre::String& Name)
   {
      ::Ogre::ManualObject::ManualObjectSection* section;

      if (!roomDecoration || Name == STRINGEMPTY)
         return -1;

      for (unsigned int i = 0; i < roomDecoration->getNumSections(); i++)
      {
         section = roomDecoration->getSection(i);

         if (section->getMaterialName() == Name)
            return (int)i;
      }

      return -1;
   };

   void ControllerRoom::CreateGeometryChunk(CLRString^ MaterialName)
   {
      const ::Ogre::String& material = StringConvert::CLRToOgre(MaterialName);
      int sectionindex		= GetRoomSectionByMaterial(material);

      // create new geometry chunk (vertexbuffer+indexbuffer+...)
      // for this material or get existing one
      if (sectionindex > -1)		
         roomManObj->beginUpdate(sectionindex);

      else
         roomManObj->begin(material, ::Ogre::RenderOperation::OT_TRIANGLE_LIST, MATERIALGROUP_ROOLOADER);

      // reset vertex counter
      verticesProcessed = 0;

      // create all sector floors and ceilings using this material
      for each (RooSector^ sector in Room->Sectors)
      {
         if (sector->MaterialNameFloor == MaterialName)
            CreateSectorPart(sector, true);

         if (sector->MaterialNameCeiling == MaterialName)
            CreateSectorPart(sector, false);
      }

      // create all side parts using this material
      for each(RooSideDef^ side in Room->SideDefs)
      {
         if (side->MaterialNameLower == MaterialName)
            CreateSidePart(side, WallPartType::Lower);

         if (side->MaterialNameMiddle == MaterialName)
            CreateSidePart(side, WallPartType::Middle);

         if (side->MaterialNameUpper == MaterialName)
            CreateSidePart(side, WallPartType::Upper);
      }

      // finish this chunk
      roomManObj->end();
   }

   void ControllerRoom::Tick(double Tick, double Span)
   {
      if (!IsInitialized)
         return;

      // fix weathernode getting hidden when going into first person
      if (weatherNode)
         weatherNode->setVisible(true);

      // process the queued subsections for recreation
      for each(CLRString^ s in recreatequeue)
         CreateGeometryChunk(s);

      // clear recreate queue
      recreatequeue->Clear();

      if (caelumSystem)
      {
         // tick caleum
         caelumSystem->updateSubcomponents((CLRReal)Span * 0.001f);

         // get moon
         Moon* moon = caelumSystem->getMoon();

         // overwrite moon phase to full moon
         if (moon)
            moon->setPhase(0.0f); 
      }
   };

   void ControllerRoom::CreateSidePart(RooSideDef^ Side, WallPartType PartType)
   {
      BgfFile^ textureFile = nullptr;
      BgfBitmap^ texture   = nullptr;
      V2 sp                = V2::ZERO;
      CLRString^ texname   = nullptr;
      CLRString^ material  = nullptr;

      /******************************************************************************/

      // select texturefile based on wallpart
      switch (PartType)
      {
      case WallPartType::Upper:
         textureFile = Side->ResourceUpper;
         texture     = Side->TextureUpper;
         texname     = Side->TextureNameUpper;
         sp          = Side->SpeedUpper;
         material    = Side->MaterialNameUpper;
         break;

      case WallPartType::Middle:
         textureFile = Side->ResourceMiddle;
         texture     = Side->TextureMiddle;
         texname     = Side->TextureNameMiddle;
         sp          = Side->SpeedMiddle;
         material    = Side->MaterialNameMiddle;
         break;

      case WallPartType::Lower:
         textureFile = Side->ResourceLower;
         texture     = Side->TextureLower;
         texname     = Side->TextureNameLower;
         sp          = Side->SpeedLower;
         material    = Side->MaterialNameLower;
         break;
      }

      /******************************************************************************/

      // check
      if (!textureFile || !texture || !material || material == STRINGEMPTY)
         return;

      // possibly create texture & material
      CreateTextureAndMaterial(texture, texname, material, sp);

      /******************************************************************************/

      // add vertexdata from walls using this sidedef
      for each(RooWall^ wall in Side->WallsLeft)
         CreateWallPart(wall, PartType, true, texture->Width, texture->Height, textureFile->ShrinkFactor);

      for each(RooWall^ wall in Side->WallsRight)
         CreateWallPart(wall, PartType, false, texture->Width, texture->Height, textureFile->ShrinkFactor);
   };

   void ControllerRoom::CreateWallPart(
      RooWall^     Wall, 
      WallPartType PartType, 
      bool         IsLeftSide, 
      int          TextureWidth, 
      int          TextureHeight, 
      int          TextureShrink)
   {
      // select side
      RooSideDef^ side = (IsLeftSide) ? Wall->LeftSide : Wall->RightSide;

      // may not have a side defined
      if (!side)
         return;

      // get vertexdata for this wallpart
      const RooWall::VertexData% vd = Wall->GetVertexData(
         PartType, 
         IsLeftSide,
         TextureWidth,
         TextureHeight,
         TextureShrink,
         SCALE);

      // P0
      roomManObj->position(vd.P0.X, vd.P0.Z, vd.P0.Y);
      roomManObj->normal(vd.Normal.X, vd.Normal.Z, vd.Normal.Y);
      roomManObj->textureCoord(vd.UV0.Y, vd.UV0.X);

      // P1
      roomManObj->position(vd.P1.X, vd.P1.Z, vd.P1.Y);
      roomManObj->normal(vd.Normal.X, vd.Normal.Z, vd.Normal.Y);
      roomManObj->textureCoord(vd.UV1.Y, vd.UV1.X);

      // P2
      roomManObj->position(vd.P2.X, vd.P2.Z, vd.P2.Y);
      roomManObj->normal(vd.Normal.X, vd.Normal.Z, vd.Normal.Y);
      roomManObj->textureCoord(vd.UV2.Y, vd.UV2.X);

      // P3
      roomManObj->position(vd.P3.X, vd.P3.Z, vd.P3.Y);
      roomManObj->normal(vd.Normal.X, vd.Normal.Z, vd.Normal.Y);
      roomManObj->textureCoord(vd.UV3.Y, vd.UV3.X);

      // create the rectangle by 2 triangles
      roomManObj->triangle(verticesProcessed, verticesProcessed + 1, verticesProcessed + 2);
      roomManObj->triangle(verticesProcessed, verticesProcessed + 2, verticesProcessed + 3);

      // increase counter
      verticesProcessed += 4;
   };

   void ControllerRoom::CreateSectorPart(RooSector^ Sector, bool IsFloor)
   {
      CLRString^ material  = nullptr;
      CLRString^ texname   = nullptr;
      V2 sp                = V2::ZERO;
      BgfFile^ textureFile = nullptr;
      BgfBitmap^ texture   = nullptr;

      /******************************************************************************/

      // ceiling
      if (!IsFloor)
      {
         textureFile = Sector->ResourceCeiling;
         texture     = Sector->TextureCeiling;
         texname     = Sector->TextureNameCeiling;
         sp          = Sector->SpeedCeiling;
         material    = Sector->MaterialNameCeiling;
      }

      // floor
      else
      {
         textureFile = Sector->ResourceFloor;
         texture     = Sector->TextureFloor;
         texname     = Sector->TextureNameFloor;
         sp          = Sector->SpeedFloor;
         material    = Sector->MaterialNameFloor;
      }

      /******************************************************************************/

      // check
      if (!textureFile || !texture || !material || material == STRINGEMPTY)
         return;

      // possibly create texture & material
      CreateTextureAndMaterial(texture, texname, material, sp);

      /******************************************************************************/

      // add vertexdata of subsectors
      for each (RooSubSector^ subSector in Sector->Leafs)
            CreateSubSector(subSector, IsFloor);
   };

   void ControllerRoom::CreateSubSector(RooSubSector^ SubSector, bool IsFloor)
   {
      // shortcuts to select basedon floor/ceiling
      array<V3>^ P;
      array<V2>^ UV;
      V3 Normal;

      if (IsFloor)
      {
         P      = SubSector->FloorP;
         UV     = SubSector->FloorUV;
         Normal = SubSector->FloorNormal;
      }
      else
      {
         P      = SubSector->CeilingP;
         UV     = SubSector->CeilingUV;
         Normal = SubSector->CeilingNormal;
      }

      // add vertices from vertexdata
      for (int i = 0; i < P->Length; i++)
      {
         roomManObj->position(0.0625f * P[i].X, 0.0625f * P[i].Z, 0.0625f * P[i].Y);
         roomManObj->textureCoord(UV[i].Y, UV[i].X);
         roomManObj->normal(Normal.X, Normal.Z, Normal.Y);
      }

      // This is a simple triangulation algorithm for convex polygons (which subsectors guarantee to be)
      // It is: Connect the first vertex with any other vertex, except for it's direct neighbours
      int triangles = P->Length - 2;

      if (IsFloor)
      {
         // forward
         for (int j = 0; j < triangles; j++)
            roomManObj->triangle(verticesProcessed + j + 2, verticesProcessed + j + 1, verticesProcessed);
      }
      else
      {
         // inverse
         for (int j = 0; j < triangles; j++)
            roomManObj->triangle(verticesProcessed, verticesProcessed + j + 1, verticesProcessed + j + 2);
      }

      // save the vertices we processed, so we know where to start triangulation next time this is called
      verticesProcessed += P->Length;
   };

   void ControllerRoom::CreateDecoration()
   {
      const float WIDTH = 10.0f;
      const float HEIGHT = 10.0f;
      const float HALFWIDTH = WIDTH / 2.0f;

      int intensity = OgreClient::Singleton->Config->DecorationIntensity;
      int numplanes = 3;
      ::Ogre::Vector3 vec(WIDTH / 2, 0, 0);
      ::Ogre::Quaternion rot;

      array<CLRString^>^ items;
      V2 A, B, C, rnd2D;
      V3 rnd3D;

      float area;
      int num;
      int randomindex;
      int vertexindex;
      ::System::Collections::Generic::List<V3>^ points;

      if (intensity <= 0)
         return;

      /**************************************************************************************/
      /*                     GENERATE RANDOM POINTS FOR GRASS MATERIALS                     */
      /**************************************************************************************/

      // loop all subsectors
      for each(RooSubSector^ subsect in Room->BSPTreeLeaves)
      {
         // try to find a decoration definition for this floortexture from lookup dictionary
         if (!grassMaterials->TryGetValue(subsect->Sector->FloorTexture, items))
            continue;

         // process triangles of this subsector
         for (int i = 0; i < subsect->Vertices->Count - 2; i++)
         {
            // pick a 2D triangle for this iteration
            // of subsector by using next 3 points of it
            A.X = (float)subsect->Vertices[0].X;
            A.Y = (float)subsect->Vertices[0].Y;
            B.X = (float)subsect->Vertices[i + 1].X;
            B.Y = (float)subsect->Vertices[i + 1].Y;
            C.X = (float)subsect->Vertices[i + 2].X;
            C.Y = (float)subsect->Vertices[i + 2].Y;

            // calc area
            area = (float)MathUtil::TriangleArea(A, B, C);

            // create an amount of grass to create for this triangle
            // scaled by the area of the triangle and intensity
            num = (int)(0.0000001f * intensity * area) + 1;

            // create num random points in triangle
            for (int k = 0; k < num; k++)
            {
               // generate random 2D point in triangle
               rnd2D = MathUtil::RandomPointInTriangle(A, B, C);

               // retrieve height for random coordinates
               // also flip y/z and scale to server/newclient
               rnd3D.X = rnd2D.X;
               rnd3D.Y = subsect->Sector->CalculateFloorHeight(rnd2D.X, rnd2D.Y, false);
               rnd3D.Z = rnd2D.Y;
               rnd3D.Scale(GeometryConstants::CLIENTFINETOKODFINE);

               // pick random decoration from mapping
               randomindex = ::System::Convert::ToInt32(
                  MathUtil::Random->NextDouble() * (items->Length - 1));

               // if this material does not yet have a section, create one
               if (!grassPoints->TryGetValue(items[randomindex], points))
               {
                  points = gcnew ::System::Collections::Generic::List<V3>();
                  grassPoints->Add(items[randomindex], points);
               }

               // add random point to according materiallist
               points->Add(rnd3D);
            }
         }
      }

      /**************************************************************************************/
      /*                                 GENERATE GRASS                                     */
      /**************************************************************************************/

      // loop grass materials with their attached randompoints
      for each(KeyValuePair<CLRString^, ::System::Collections::Generic::List<V3>^> pair in grassPoints)
      {
         // create a new subsection for all grass using this material
         roomDecoration->begin(StringConvert::CLRToOgre(pair.Key), ::Ogre::RenderOperation::OT_TRIANGLE_LIST, MATERIALGROUP_ROOLOADER);

         // reset vertexcounter
         vertexindex = 0;

         // how often we are going to call position() and triangle() (1tri=3indx) below
         int numVertices = pair.Value->Count * numplanes * 4;

         // set buffersizes accordingly to avoid dynamic resizing
         roomDecoration->estimateVertexCount((size_t)numVertices);
         roomDecoration->estimateIndexCount((size_t)(3 * numVertices));

         // loop points
         for each(V3 p in pair.Value)
         {
            // rotate by this for each grassplane
            rot.FromAngleAxis(
               ::Ogre::Degree(180.0f / (float)numplanes), ::Ogre::Vector3::UNIT_Y);

            for (int j = 0; j < numplanes; ++j)
            {
               roomDecoration->position(p.X - vec.x, p.Y + HEIGHT, p.Z - vec.z);
               roomDecoration->textureCoord(0, 0);

               roomDecoration->position(p.X + vec.x, p.Y + HEIGHT, p.Z + vec.z);
               roomDecoration->textureCoord(1, 0);

               roomDecoration->position(p.X - vec.x, p.Y, p.Z - vec.z);
               roomDecoration->textureCoord(0, 1);

               roomDecoration->position(p.X + vec.x, p.Y, p.Z + vec.z);
               roomDecoration->textureCoord(1, 1);

               // front side
               roomDecoration->triangle(vertexindex, vertexindex + 3, vertexindex + 1);
               roomDecoration->triangle(vertexindex, vertexindex + 2, vertexindex + 3);

               // back side
               roomDecoration->triangle(vertexindex + 1, vertexindex + 3, vertexindex);
               roomDecoration->triangle(vertexindex + 3, vertexindex + 2, vertexindex);

               // rotate grassplane for next iteration
               vec = rot * vec;

               // increase vertexcounter
               vertexindex += 4;
            }
         }

         // finish this subsection
         roomDecoration->end();
      }
   };

   void ControllerRoom::CreateTextureAndMaterial(BgfBitmap^ Texture, CLRString^ TextureName, CLRString^ MaterialName, V2% ScrollSpeed)
   {
      if (!Texture || !TextureName || !MaterialName || TextureName == STRINGEMPTY || MaterialName == STRINGEMPTY)
         return;

      ::Ogre::String& ostr_texname = StringConvert::CLRToOgre(TextureName);
      ::Ogre::String& ostr_matname = StringConvert::CLRToOgre(MaterialName);

      // possibly create texture
      Util::CreateTextureA8R8G8B8(Texture, ostr_texname, TEXTUREGROUP_ROOLOADER, MIP_DEFAULT);
        
      // scrolling texture data
      Vector2* scrollSpeed = &Util::ToOgre(ScrollSpeed);

      // possibly create material
      if (waterTextures->Contains(TextureName))
      {
         Util::CreateMaterialWater(
            ostr_matname, ostr_texname,
            MATERIALGROUP_ROOLOADER,
            scrollSpeed);
      }
      else
      {
         Util::CreateMaterialRoom(
            ostr_matname, ostr_texname,
            MATERIALGROUP_ROOLOADER,
            scrollSpeed);
      }
   };

   void ControllerRoom::OnRooFileWallTextureChanged(System::Object^ sender, WallTextureChangedEventArgs^ e)
   {
      if (!e || !e->ChangedSide)
         return;

      /******************************************************************************/

      CLRString^ material = nullptr;
      CLRString^ texname  = nullptr;
      BgfBitmap^ texture         = nullptr;
      V2 scrollspeed             = V2::ZERO;

      /******************************************************************************/

      switch (e->WallPartType)
      {
      case WallPartType::Upper:
         texture     = e->ChangedSide->TextureUpper;
         scrollspeed = e->ChangedSide->SpeedUpper;
         texname     = e->ChangedSide->TextureNameUpper;
         material    = e->ChangedSide->MaterialNameUpper;
         break;

      case WallPartType::Middle:
         texture     = e->ChangedSide->TextureMiddle;
         scrollspeed = e->ChangedSide->SpeedMiddle;
         texname     = e->ChangedSide->TextureNameMiddle;
         material    = e->ChangedSide->MaterialNameMiddle;
         break;

      case WallPartType::Lower:
         texture     = e->ChangedSide->TextureLower;
         scrollspeed = e->ChangedSide->SpeedLower;
         texname     = e->ChangedSide->TextureNameLower;
         material    = e->ChangedSide->MaterialNameLower;
         break;
      }

      // no materialchange? nothing to do
      if (e->OldMaterialName == material)
         return;

      // possibly create new texture and material
      CreateTextureAndMaterial(texture, texname, material, scrollspeed);

      // enqueue old material subsection for recreation
      if (!recreatequeue->Contains(e->OldMaterialName))
         recreatequeue->Add(e->OldMaterialName);

      // enqueue new material subsection for recreation
      if (!recreatequeue->Contains(material))
         recreatequeue->Add(material);
   };

   void ControllerRoom::OnRooFileSectorTextureChanged(System::Object^ sender, SectorTextureChangedEventArgs^ e)
   {
      if (!e || !e->ChangedSector)
         return;

      /******************************************************************************/

      CLRString^ material = nullptr;
      CLRString^ texname  = nullptr;
      BgfBitmap^ texture         = nullptr;
      V2 scrollspeed             = V2::ZERO;

      /******************************************************************************/

      // floor
      if (e->IsFloor)
      {
         texture     = e->ChangedSector->TextureFloor;
         scrollspeed = e->ChangedSector->SpeedFloor;
         texname     = e->ChangedSector->TextureNameFloor;
         material    = e->ChangedSector->MaterialNameFloor;
      }

      // ceiling
      else if (!e->IsFloor)
      {
         texture     = e->ChangedSector->TextureCeiling;
         scrollspeed = e->ChangedSector->SpeedCeiling;
         texname     = e->ChangedSector->TextureNameCeiling;
         material    = e->ChangedSector->MaterialNameCeiling;
      }

      // no materialchange? nothing to do
      if (e->OldMaterialName == material)
         return;

      // possibly create new texture and material
      CreateTextureAndMaterial(texture, texname, material, scrollspeed);

      // enqueue old material subsection for recreation
      if (!recreatequeue->Contains(e->OldMaterialName))
         recreatequeue->Add(e->OldMaterialName);

      // enqueue new material subsection for recreation
      if (!recreatequeue->Contains(material))
         recreatequeue->Add(material);
   };

   void ControllerRoom::OnRooFileSectorMoved(System::Object^ sender, SectorMovedEventArgs^ e)
   {
      if (!e || !e->Sector)
         return;

      /******************************************************************************/

      // possibly add floor material to recreation
      if (e->Sector->MaterialNameFloor &&
         e->Sector->MaterialNameFloor != STRINGEMPTY && 
         !recreatequeue->Contains(e->Sector->MaterialNameFloor))
      {
         recreatequeue->Add(e->Sector->MaterialNameFloor);
      }

      // possibly add ceiling material to recreation
      if (e->Sector->MaterialNameCeiling &&
         e->Sector->MaterialNameCeiling != STRINGEMPTY &&
         !recreatequeue->Contains(e->Sector->MaterialNameCeiling))
      {
         recreatequeue->Add(e->Sector->MaterialNameCeiling);
      }

      // possibly affected sides to recreation
      for each(RooSideDef^ side in e->Sector->Sides)
      {
         if (side->MaterialNameLower &&
            side->MaterialNameLower != STRINGEMPTY &&
            !recreatequeue->Contains(side->MaterialNameLower))
         {
            recreatequeue->Add(side->MaterialNameLower);
         }

         if (side->MaterialNameMiddle &&
            side->MaterialNameMiddle != STRINGEMPTY &&
            !recreatequeue->Contains(side->MaterialNameMiddle))
         {
            recreatequeue->Add(side->MaterialNameMiddle);
         }

         if (side->MaterialNameUpper &&
            side->MaterialNameUpper != STRINGEMPTY &&
            !recreatequeue->Contains(side->MaterialNameUpper))
         {
            recreatequeue->Add(side->MaterialNameUpper);
         }
      }
   };

   void ControllerRoom::OnDataPropertyChanged(Object^ sender, PropertyChangedEventArgs^ e)
   {
      if (!IsInitialized)
         return;

      // it is very important to create the avatar here instead of ListChanged event add.
      // That is because creating the avatar will set the camera and with it will update
      // ViewerPosition in DataController, which then will update any instance in RoomObjects list
      // If this was placed in the OnListChanged-Add handler here instead, then the adminui datagrid roomobjects
      // crashes because it will receive a 'modified' event for an entry, which it has not added yet , because
      // its add handler would occur after the addhandler here in this class and hence after the modify.
      if (CLRString::Equals(e->PropertyName, DataController::PROPNAME_AVATAROBJECT))
      {
         RoomObject^ avatar = OgreClient::Singleton->Data->AvatarObject;

         // set
         if (avatar)
            RoomObjectAdd(avatar);
      }
   };

   void ControllerRoom::OnEffectSnowingPropertyChanged(Object^ sender, PropertyChangedEventArgs^ e)
   {
      if (!IsInitialized || !particleSysSnow || !Room)
         return;

      if (CLRString::Equals(e->PropertyName, EffectSnowing::PROPNAME_ISACTIVE) &&
         !OgreClient::Singleton->Config->DisableWeatherEffects)
      {
         // start or stop snow weather
         if (OgreClient::Singleton->Data->Effects->Snowing->IsActive)
         {
            // possibly attach to roomnode
            if (!particleSysSnow->isAttached())
               weatherNode->attachObject(particleSysSnow);

            if (particleSysSnow->getNumTechniques() > 0)
            {
               // get technique
               ::ParticleUniverse::ParticleTechnique* technique = 
                  particleSysSnow->getTechnique(0);

               // modify observer 0
               // setup observer threshold, start observing particles
               // once they entered the roomboundingbox height
               if (technique->getNumObservers() > 0)
               {
                  ::ParticleUniverse::OnPositionObserver* observer = (::ParticleUniverse::OnPositionObserver*)
                     technique->getObserver(0);

                  if (observer)
                  {
                     // get room bounding box
                     BoundingBox3D^ bBox = Room->GetBoundingBox3D(true);

                     // turn max into ogre world (scale, flip)
                     ::Ogre::Vector3 max = Util::ToOgreYZFlipped(bBox->Max) * SCALE;

                     // set threshold
                     observer->setPositionYThreshold(max.y + 5.0f);
                  }
               }
            }

            // start it
            particleSysSnow->start();
         }
         else
         {
            // fade stop it so it can be restarted without all particles deleted
            particleSysSnow->stopFade();

            // possibly detach from parent
            if (particleSysSnow->isAttached())
               particleSysSnow->detachFromParent();
         }
      }
   };

   void ControllerRoom::OnEffectRainingPropertyChanged(Object^ sender, PropertyChangedEventArgs^ e)
   {
      if (!IsInitialized || !particleSysRain || !Room)
         return;

      if (CLRString::Equals(e->PropertyName, EffectRaining::PROPNAME_ISACTIVE) &&
         !OgreClient::Singleton->Config->DisableWeatherEffects)
      {
         // start or stop rain weather
         if (OgreClient::Singleton->Data->Effects->Raining->IsActive)
         {
            // possibly attach to roomnode
            if (!particleSysRain->isAttached())
               weatherNode->attachObject(particleSysRain);

            if (particleSysRain->getNumTechniques() > 0)
            {
               // get technique
               ::ParticleUniverse::ParticleTechnique* technique =
                  particleSysRain->getTechnique(0);

               // modify observer 0
               // setup observer threshold, start observing particles
               // once they entered the roomboundingbox height
               if (technique->getNumObservers() > 0)
               {
                  ::ParticleUniverse::OnPositionObserver* observer = (::ParticleUniverse::OnPositionObserver*)
                     technique->getObserver(0);

                  if (observer)
                  {
                     // get room bounding box
                     BoundingBox3D^ bBox = Room->GetBoundingBox3D(true);

                     // turn max into ogre world (scale, flip)
                     ::Ogre::Vector3 max = Util::ToOgreYZFlipped(bBox->Max) * SCALE;

                     // set threshold
                     observer->setPositionYThreshold(max.y + 5.0f);
                  }
               }
            }

            // start it
            particleSysRain->start();
         }
         else
         {
            // fade stop it so it can be restarted without all particles deleted
            particleSysRain->stopFade();

            // possibly detach from parent
            if (particleSysRain->isAttached())
               particleSysRain->detachFromParent();
         }
      }
   };

   void ControllerRoom::UpdateSky()
   {
      // examine the background bgf
      CLRString^ bgfFilename =
         OgreClient::Singleton->Data->RoomInformation->BackgroundFile;

      char* material = 0;
      bool isfrenzy = false;

      if (bgfFilename->Contains(SKY_DAY))
         material = SKY_DAY_MAT;

      else if (bgfFilename->Contains(SKY_EVENING))
         material = SKY_EVENING_MAT;

      else if (bgfFilename->Contains(SKY_MORNING))
         material = SKY_MORNING_MAT;

      else if (bgfFilename->Contains(SKY_NIGHT))
         material = SKY_NIGHT_MAT;

      else if (bgfFilename->Contains(SKY_FRENZY))
      {
         material = SKY_FRENZY_MAT;
         isfrenzy = true;
      }

      // set or disable simple skybox
      if (material && OgreClient::Singleton->Config->DisableNewSky)
         SceneManager->setSkyBox(true, material);
      else
         SceneManager->setSkyBox(false, "");

      // use red as background color for frenzy mode
      // due to caleum skydome transparent at night
      if (OgreClient::Singleton->Viewport)
      {
         if (!isfrenzy)
         {
            OgreClient::Singleton->Viewport->setBackgroundColour(
               ::Ogre::ColourValue(0.0f, 0.0f, 0.0f, 0.0f));
         }
         else
         {
            OgreClient::Singleton->Viewport->setBackgroundColour(
               ::Ogre::ColourValue(0.6f, 0.0f, 0.0f, 0.0f));
         }
      }
   };

   void ControllerRoom::AdjustOctree()
   {
      // get room boundingbox
      BoundingBox3D^ bbBox = Room->GetBoundingBox3D(true);

      // scaled and flipped ogre variants
      ::Ogre::Vector3 min = Util::ToOgreYZFlipped(bbBox->Min) * 0.0625f + ::Ogre::Vector3(64.0f, 0, 64.0f) + ::Ogre::Vector3(-1.0f, -1.0f, -1.0f);
      ::Ogre::Vector3 max = Util::ToOgreYZFlipped(bbBox->Max) * 0.0625f + ::Ogre::Vector3(64.0f, 0, 64.0f) + ::Ogre::Vector3(1.0f, 1.0f, 1.0f);
      ::Ogre::Vector3 diff = max - min;

      // get biggest side
      float maxSide = System::Math::Max((float)diff.x, System::Math::Max((float)diff.y, (float)diff.z));

      // the new maximum based on biggest side
      ::Ogre::Vector3 newMax = ::Ogre::Vector3(min.x + maxSide, min.y + maxSide, min.z + maxSide);

      // adjust size of octree to an cube using max-side
      const AxisAlignedBox octreeBox = AxisAlignedBox(min, newMax);
      SceneManager->setOption("Size", &octreeBox);

#ifdef DEBUGOCTREE
      const bool showOctree = true;
      SceneManager->setOption("ShowOctree", &showOctree);
#endif

      // update caelum heights
      if (caelumSystem)
      {
         ::Caelum::CloudSystem* clouds = caelumSystem->getCloudSystem();

         if (clouds)
         {
            if (clouds->getLayerCount() > 0)
               clouds->getLayer(0)->setHeight(newMax.y + 2000.0f);

            if (clouds->getLayerCount() > 1)
               clouds->getLayer(1)->setHeight(newMax.y + 5000.0f);
         }
      }
   };

   void ControllerRoom::AdjustAmbientLight()
   {
      unsigned char ambient		= OgreClient::Singleton->Data->RoomInformation->AmbientLight;
      unsigned char avatar		= OgreClient::Singleton->Data->RoomInformation->AvatarLight;
      unsigned char directional	= OgreClient::Singleton->Data->LightShading->LightIntensity;

      // simply use the maximum of avatarlight (nightvision..) and ambientlight for ambientlight
      unsigned char max = System::Math::Max(ambient, avatar);

      // allow user to alter the client's ambient and directional light
      float brightnessFactor = Math::Clamp(OgreClient::Singleton->Config->BrightnessFactor + 1.0f, 1.0f, 1.8f);

      // adjust ambientlight
      SceneManager->setAmbientLight(brightnessFactor * Util::LightIntensityToOgreRGB(max));

      // log
      Logger::Log(MODULENAME, LogType::Info,
         "Setting AmbientLight to " + max.ToString());

      // directional sun of Caelum
      if (caelumSystem)
      {
         ::Caelum::BaseSkyLight* sun  = caelumSystem->getSun();
         ::Caelum::BaseSkyLight* moon = caelumSystem->getMoon();

         ::Ogre::ColourValue color = brightnessFactor * 3.0f * Util::LightIntensityToOgreRGB(directional);

         if (sun)
         {
            sun->setDiffuseMultiplier(color);
            sun->setSpecularMultiplier(color);
         }

         if (moon)
         {
            moon->setDiffuseMultiplier(color);
            moon->setSpecularMultiplier(color);
         }

         // log
         Logger::Log(MODULENAME, LogType::Info,
            "Setting DirectionalLight to " + directional.ToString());
      }
   };

   void ControllerRoom::ProjectileAdd(Projectile^ Projectile)
   {
      // create 2d projectile
      ProjectileNode2D^ newObject = gcnew ProjectileNode2D(Projectile, SceneManager);

      // attach a reference to the RemoteNode instance to the basic model
      Projectile->UserData = newObject;          
   };

   void ControllerRoom::ProjectileRemove(Projectile^ Projectile)
   {
      // try to cast remotenode attached to userdata
      ProjectileNode2D^ engineObject = dynamic_cast<ProjectileNode2D^>(Projectile->UserData);

      // dispose
      if (engineObject)
         delete engineObject;

      // remove reference
      Projectile->UserData = nullptr;
   };

   void ControllerRoom::RoomObjectAdd(RoomObject^ roomObject)
   {
      // remotenode we're creating
      RemoteNode^ newObject;

      // the name of the 3d model .xml if existant
      CLRString^ mainOverlay      = roomObject->OverlayFile->Replace(FileExtensions::BGF, FileExtensions::XML);
      ::Ogre::String& ostr_mainOverlay = StringConvert::CLRToOgre(mainOverlay);

      // Check if there is a 3D model available
      if (ResourceGroupManager::getSingletonPtr()->resourceExists(RESOURCEGROUPMODELS, ostr_mainOverlay))
      {
         // log
         //Logger::Log(MODULENAME, LogType::Info,
         //    "Adding 3D object " + roomObject->ID.ToString() + " (" + roomObject->Name + ") to scene.");

         // 3d model
         newObject = gcnew RemoteNode3D(roomObject, SceneManager);
      }
      else
      {
         // log
         //Logger::Log(MODULENAME, LogType::Info,
         //    "Adding 2D object " + roomObject->ID.ToString() + " (" + roomObject->Name + ") to scene.");

         // legacy object
         newObject = gcnew RemoteNode2D(roomObject, SceneManager);
      }

      // attach a reference to the RemoteNode instance to the basic model
      roomObject->UserData = newObject;

      // check if this is our avatar we're controlling
      if (roomObject->IsAvatar)
      {
         // log
         Logger::Log(MODULENAME, LogType::Info,
            "Found own avatar: " + roomObject->ID.ToString() + " (" + roomObject->Name + ")");

         // save a reference to the avatar object
         AvatarObject = newObject;
      }
   };

   void ControllerRoom::RoomObjectRemove(RoomObject^ roomObject)
   {
      // log
      //Logger::Log(MODULENAME, LogType::Info,
      //    "Removing object " + roomObject->ID.ToString() + " (" + roomObject->Name + ")" + " from scene.");

      // try to cast remotenode attached to userdata
      RemoteNode^ engineObject = dynamic_cast<RemoteNode^>(roomObject->UserData);

         // reset avatar reference in case it was removed
      if (roomObject->IsAvatar)
      {
         AvatarObject = nullptr;

      }

      // dispose
      if (engineObject)
         delete engineObject;

      // remove reference
      roomObject->UserData = nullptr;
   };

   void ControllerRoom::OnProjectilesListChanged(Object^ sender, ListChangedEventArgs^ e)
   {
      switch (e->ListChangedType)
      {
      case System::ComponentModel::ListChangedType::ItemAdded:
         ProjectileAdd(OgreClient::Singleton->Data->Projectiles[e->NewIndex]);
         break;

      case System::ComponentModel::ListChangedType::ItemDeleted:
         ProjectileRemove(OgreClient::Singleton->Data->Projectiles->LastDeletedItem);
         break;
      }
   };

   void ControllerRoom::OnRoomObjectsListChanged(Object^ sender, ListChangedEventArgs^ e)
   {
      RoomObject^ o;
      switch (e->ListChangedType)
      {
      case System::ComponentModel::ListChangedType::ItemAdded:
         o = OgreClient::Singleton->Data->RoomObjects[e->NewIndex];
         // skip avatar, was already created
         if (!o->IsAvatar)
            RoomObjectAdd(o);
         break;

      case System::ComponentModel::ListChangedType::ItemDeleted:
         RoomObjectRemove(OgreClient::Singleton->Data->RoomObjects->LastDeletedItem);
         break;
      }
   };

   void ControllerRoom::HandleGameModeMessage(GameModeMessage^ Message)
   {
      switch ((MessageTypeGameMode)Message->PI)
      {
      case MessageTypeGameMode::Player:
         HandlePlayerMessage((PlayerMessage^)Message);
         break;

      case MessageTypeGameMode::LightAmbient:
         HandleLightAmbient((LightAmbientMessage^)Message);
         break;

      case MessageTypeGameMode::LightPlayer:
         HandleLightPlayer((LightPlayerMessage^)Message);
         break;

      case MessageTypeGameMode::LightShading:
         HandleLightShading((LightShadingMessage^)Message);
         break;

      case MessageTypeGameMode::Background:
         HandleBackground((BackgroundMessage^)Message);
         break;

      default:
            break;
      }
   };

   void ControllerRoom::HandlePlayerMessage(PlayerMessage^ Message)
   {
      // unload the current scene
      UnloadRoom();

      // load new scene
      LoadRoom();
   };

   void ControllerRoom::HandleLightAmbient(LightAmbientMessage^ Message)
   {
      AdjustAmbientLight();  
   };

   void ControllerRoom::HandleLightPlayer(LightPlayerMessage^ Message)
   {
      AdjustAmbientLight();
   };

   void ControllerRoom::HandleLightShading(LightShadingMessage^ Message)
   {
      AdjustAmbientLight();
   };

   void ControllerRoom::HandleBackground(BackgroundMessage^ Message)
   {
      UpdateSky();
   };

   void ControllerRoom::LoadImproveData()
   {
      //////////////////////// PATHS ////////////////////////////////////////////

      // build path to decoration resource path
      CLRString^ path = Path::Combine(
         OgreClient::Singleton->Config->ResourcesPath, RESOURCEGROUPDECORATION);

      /////////////////////// GRASS ///////////////////////////////////////////////

      // path to grass.xml
      CLRString^ grasspath = Path::Combine(path, "grass/grass.xml");
        
      // dont go on if file missing
      if (!System::IO::File::Exists(grasspath))
      {
         // log
         Logger::Log(MODULENAME, LogType::Warning,
            "grass.xml decoration file missing");

         return;
      }

      // dictionary to store sets definition
      Dictionary<unsigned int, List<CLRString^>^>^ grasssets = 
         gcnew Dictionary<unsigned int, List<CLRString^>^>();

      // store parsed ids
      unsigned int texid = 0;
      unsigned int setid = 0;

      // temporary used
      List<CLRString^>^ grassset; 

      // create reader
      XmlReader^ reader = XmlReader::Create(grasspath);

      // rootnode
      reader->ReadToFollowing("grass");

      // sets
      reader->ReadToFollowing("sets");

      // loop sets
      if (reader->ReadToDescendant("set"))
      {
         do
         {
            // valid id
            if (::System::UInt32::TryParse(reader["id"], setid))
            {
               // create material list
               grassset = gcnew List<CLRString^>();

               // loop materials
               if (reader->ReadToDescendant("material"))
               {
                  do
                  {
                     // add material to set
                     grassset->Add(reader["name"]);
                  }
                  while (reader->ReadToNextSibling("material"));
               }

               // add set to sets
               grasssets->Add(setid, grassset);
            }
         }
         while (reader->ReadToNextSibling("set"));
      }

      // mappings
      reader->ReadToFollowing("mappings");

      // loop mappings
      if (reader->ReadToDescendant("texture"))
      {
         do
         {
            // try get texid and setid
            if (::System::UInt32::TryParse(reader["id"], texid) &&
               ::System::UInt32::TryParse(reader["set"], setid))
            {
               if (grasssets->TryGetValue(setid, grassset))
               {
                  grassMaterials->Add(texid, grassset->ToArray());
               }
            }
         }
         while (reader->ReadToNextSibling("texture"));
      }

      // finish read
      reader->Close();

      /////////////////////// WATER ///////////////////////////////////////////////

      // path to water.xml
      CLRString^ waterpath = Path::Combine(path, "water.xml");

      // dont go on if file missing
      if (!System::IO::File::Exists(waterpath))
      {
         // log
         Logger::Log(MODULENAME, LogType::Warning,
            "water.xml file missing");

         return;
      }

      // create reader
      reader = XmlReader::Create(waterpath);

      // rootnode
      reader->ReadToFollowing("water");

      // loop entries
      if (reader->ReadToDescendant("texture"))
      {
         do
         {
            waterTextures->Add(reader["name"]);
         }
         while (reader->ReadToNextSibling("texture"));
      }

      // finish read
      reader->Close();
   };
};};
