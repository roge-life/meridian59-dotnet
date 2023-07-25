#include "stdafx.h"

namespace Meridian59 { namespace Ogre 
{
   static RemoteNode::RemoteNode()
   {
   };

   RemoteNode::RemoteNode(Data::Models::RoomObject^ RoomObject, ::Ogre::SceneManager* SceneManager)
   {
      roomObject = RoomObject;
      sceneManager = SceneManager;

      // create sound holder list
      sounds = new std::list<ISound*>();

      // create scenenode
      const ::Ogre::String& ostr_scenenodename = 
         PREFIX_REMOTENODE_SCENENODE + ::Ogre::StringConverter::toString(roomObject->ID);
      Logger::Log(MODULENAME, LogType::Info, "createremotescenenode" + toString(roomObject->ID));
      try {
      SceneNode = SceneManager->getRootSceneNode()->createChildSceneNode(ostr_scenenodename);
      }
      catch(...) {
	      Logger::Log(MODULENAME, LogType::Info, "caught exception in RemoteNode on scene " + toString(roomObject->ID));
      }
	      
      SceneNode->setFixedYawAxis(true);

      // initial position and orientation
      RefreshPosition();
      RefreshOrientation();

      // show boundingbox in debug builds
#if DEBUGBOUNDINGBOX
      SceneNode->showBoundingBox(true);
#endif

      // special handling for avatar (attach camera)
      if (RoomObject->IsAvatar)
      {
         ::Ogre::SceneNode* cameraNode = OgreClient::Singleton->CameraNode;

         // attach cameranode on avatarnode
         SceneNode->addChild(cameraNode);
         SceneNode->setFixedYawAxis(true);

         // enable camera listener and trigger update
         OgreClient::Singleton->IsCameraListenerEnabled = true;
         cameraNode->_update(true, true);

         // set this node as sound listener
         ControllerSound::SetListenerNode(this);

         // set initial visibility
         SceneNode->setVisible(!ControllerInput::IsCameraFirstPerson);

         // if we've hidden the avatar-scenenode due to 1.person above
         // make sure a light attached is still visible!
         if (Light)
            Light->setVisible(true);
      }

      // attach listener
      RoomObject->PropertyChanged +=
         gcnew PropertyChangedEventHandler(this, &RemoteNode::OnRoomObjectPropertyChanged);

      // possibly create a name and quest marker
      UpdateName();
      UpdateQuestMarker();
   };

   RemoteNode::~RemoteNode()
   {
      // special handling for avatar, detach camera
      if (roomObject->IsAvatar)
      {
         // unset 3d sound listenernode
         ControllerSound::SetListenerNode(nullptr);

         // unset camera listener
         OgreClient::Singleton->IsCameraListenerEnabled = false;

         // detach cameranode from avatar
         SceneNode->removeChild(OgreClient::Singleton->CameraNode);
      }

      // detach listener
      RoomObject->PropertyChanged -= 
         gcnew PropertyChangedEventHandler(this, &RemoteNode::OnRoomObjectPropertyChanged);

      // LIGHT FIRST! 
      DestroyLight();

      ::Ogre::String& ostr_nodeid = 
         PREFIX_REMOTENODE_SCENENODE + ::Ogre::StringConverter::toString(roomObject->ID);

      // cleanup attached name
      if (billboardSetName)
      {
         billboardSetName->clear();
         billboardSetName->detachFromParent();
         SceneManager->destroyBillboardSet(billboardSetName);
      }

      // cleanup attached quest marker
      if (billboardSetQuestMarker)
      {
         billboardSetQuestMarker->clear();
         billboardSetQuestMarker->detachFromParent();
         SceneManager->destroyBillboardSet(billboardSetQuestMarker);
      }

      // cleanup scenenode
      if (SceneManager->hasSceneNode(ostr_nodeid))
         SceneManager->destroySceneNode(ostr_nodeid);

      // cleanup attached sounds
      if (sounds)
      {
         for(std::list<ISound*>::iterator it = sounds->begin(); it != sounds->end(); ++it)
         {
            ISound* sound = *it;
            sound->stop();
            sound->drop();
         }

         sounds->clear();
         delete sounds;
	   }

      sceneNode         = nullptr;
      sounds            = nullptr;
      billboardSetName  = nullptr;
      billboardName     = nullptr;
      billboardSetQuestMarker = nullptr;
      billboardQuestMarker = nullptr;
      sceneManager      = nullptr;
   };

   void RemoteNode::OnRoomObjectPropertyChanged(Object^ sender, PropertyChangedEventArgs^ e)
   {
      // update scenenode orientation based on datalayer model     
      if (CLRString::Equals(e->PropertyName, Data::Models::RoomObject::PROPNAME_ANGLE))
         RefreshOrientation();

      // Update light if changed
      else if (CLRString::Equals(e->PropertyName, Data::Models::RoomObject::PROPNAME_LIGHTINGINFO))
      {
         if (RoomObject->LightingInfo->IsLightOn)
         {
            DestroyLight();
            CreateLight();
         }
         else
            DestroyLight();
      }

      else if (CLRString::Equals(e->PropertyName, Data::Models::RoomObject::PROPNAME_POSITION3D))
      {
         RefreshPosition();
      }

      else if (CLRString::Equals(e->PropertyName, Data::Models::RoomObject::PROPNAME_NAME))
      {
         UpdateName();
         // might have to move quest marker
         UpdateQuestMarker();
      }

      else if (CLRString::Equals(e->PropertyName, Data::Models::RoomObject::PROPNAME_FLAGS))
      {
         UpdateName();
         UpdateQuestMarker();
         UpdateMaterial();
      }

      else if (CLRString::Equals(e->PropertyName, Data::Models::RoomObject::PROPNAME_ISTARGET))
      {
         UpdateMaterial();
      }

      else if (CLRString::Equals(e->PropertyName, Data::Models::RoomObject::PROPNAME_ISHIGHLIGHTED))
      {
         UpdateMaterial();
      }
    };

   void RemoteNode::CreateLight()
   {
      ::Ogre::String& ostr_ligtname = 
         PREFIX_REMOTENODE_LIGHT + ::Ogre::StringConverter::toString(roomObject->ID);

      Light = Util::CreateLight(RoomObject, SceneManager, ostr_ligtname);

      if (Light != nullptr)
      {
         // maximum distance we render this light or skip it
         Light->setRenderingDistance(MAXLIGHTRENDERDISTANCE);
         SceneNode->attachObject(Light);
      }
   };

   void RemoteNode::UpdateLight()
   {
      if (Light != nullptr)
      {
         // adjust the light from M59 values (light class extension)
         Util::UpdateFromILightOwner(*Light, RoomObject);
      }
   };

   void RemoteNode::DestroyLight()
   {
      ::Ogre::String& ostr_ligtname = 
         PREFIX_REMOTENODE_LIGHT + ::Ogre::StringConverter::toString(roomObject->ID);

      if (SceneManager->hasLight(ostr_ligtname))
         SceneManager->destroyLight(ostr_ligtname);

      Light = nullptr;
   };

   void RemoteNode::CreateName()
   {
      ::Ogre::String& ostr_billboard = 
         PREFIX_NAMETEXT_BILLBOARD + ::Ogre::StringConverter::toString(roomObject->ID);

      // create BillboardSet for name
      billboardSetName = sceneManager->createBillboardSet(ostr_billboard, 1);
      billboardSetName->setBillboardOrigin(BillboardOrigin::BBO_BOTTOM_CENTER);
      billboardSetName->setBillboardType(BillboardType::BBT_POINT);
      billboardSetName->setUseAccurateFacing(false);
      billboardSetName->setAutoextend(false);

      // no boundingbox for billboardset (we don't wanna catch rays with name)
      billboardSetName->setBounds(AxisAlignedBox::BOX_NULL, 0.0f);
        
      // create Billboard
      billboardName = billboardSetName->createBillboard(::Ogre::Vector3::ZERO);
      billboardName->setColour(ColourValue::ZERO);

      // attach name billboardset to object
      SceneNode->attachObject(billboardSetName);
   };

   void RemoteNode::UpdateName()
   {
#ifdef VANILLA
      bool showName = RoomObject->Flags->IsPlayer;
#else
      bool showName = RoomObject->Flags->IsDisplayName;
#endif
      if (showName && 
         !(RoomObject->Flags->Drawing == ObjectFlags::DrawingType::Invisible) &&
         RoomObject->Name != nullptr &&
         !CLRString::Equals(RoomObject->Name, CLRString::Empty) &&
         !(RoomObject->IsAvatar && ControllerInput::IsCameraFirstPerson))
      {
         // create or reactivate name text
         if (billboardSetName == nullptr)
            CreateName();

         else
            billboardSetName->setVisible(true);

         // start update
         CLRString^ strTex = PREFIX_NAMETEXT_TEXTURE + RoomObject->Name + 
            "/" + NameColors::GetColorFor(RoomObject->Flags).ToString();

         CLRString^ strMat = PREFIX_NAMETEXT_MATERIAL + RoomObject->Name + 
            "/" + NameColors::GetColorFor(RoomObject->Flags).ToString();

         ::Ogre::String& texName = StringConvert::CLRToOgre(strTex);
         ::Ogre::String& matName = StringConvert::CLRToOgre(strMat);

         // create Texture and material
         TextureManager& texMan = TextureManager::getSingleton();

         if (!texMan.resourceExists(texName))
         {
            // create bitmap to draw on
            System::Drawing::Bitmap^ bitmap = 
               Meridian59::Drawing2D::ImageComposerGDI<Data::Models::ObjectBase^>::NameBitmap::Get(RoomObject);

            // create texture from bitmap
            Util::CreateTexture(bitmap, texName, TEXTUREGROUP_MOVABLETEXT);
            
            // cleanup
            delete bitmap;
         }

         Util::CreateMaterialLabel(matName, texName, MATERIALGROUP_MOVABLETEXT);

         // set material
         billboardSetName->setMaterialName(matName);

         // get size from texture
         TexturePtr texPtr = Ogre::static_pointer_cast<Ogre::Texture>(
            texMan.createOrRetrieve(texName, TEXTUREGROUP_MOVABLETEXT).first);

         nameTextureWidth = (float)texPtr->getWidth();
         nameTextureHeight = (float)texPtr->getHeight();

         billboardSetName->setDefaultDimensions(nameTextureWidth, nameTextureHeight);
         billboardSetName->setBounds(AxisAlignedBox::BOX_NULL, 0.0f);

         // update position of name
         UpdateNamePosition();

         texPtr.reset();
      }

      else if (billboardSetName != nullptr)
      {
         // hide it
         billboardSetName->setVisible(false);
      }
   };

   void RemoteNode::CreateQuestMarker()
   {
      ::Ogre::String& ostr_billboard = PREFIX_QUESTMARKER_BILLBOARD + ::Ogre::StringConverter::toString(roomObject->ID);

      // create BillboardSet for quest marker
      billboardSetQuestMarker = sceneManager->createBillboardSet(ostr_billboard, 1);
      billboardSetQuestMarker->setBillboardOrigin(BillboardOrigin::BBO_BOTTOM_CENTER);
      billboardSetQuestMarker->setBillboardType(BillboardType::BBT_POINT);
      billboardSetQuestMarker->setUseAccurateFacing(false);
      billboardSetQuestMarker->setAutoextend(false);

      // no boundingbox for billboardset (we don't wanna catch rays with quest marker)
      billboardSetQuestMarker->setBounds(AxisAlignedBox::BOX_NULL, 0.0f);

      // create Billboard
      billboardQuestMarker = billboardSetQuestMarker->createBillboard(::Ogre::Vector3::ZERO);
      billboardQuestMarker->setColour(ColourValue::ZERO);

      // attach quest marker billboardset to object
      SceneNode->attachObject(billboardSetQuestMarker);
   };

   void RemoteNode::UpdateQuestMarker()
   {
      bool showQuestMarker = (RoomObject->Flags->IsNPCActiveQuest || RoomObject->Flags->IsNPCHasQuests || RoomObject->Flags->IsMobKillQuest);

      if (showQuestMarker &&
         !(RoomObject->Flags->Drawing == ObjectFlags::DrawingType::Invisible) &&
         !(RoomObject->IsAvatar && ControllerInput::IsCameraFirstPerson))
      {
         // create or reactivate quest marker text
         if (billboardSetQuestMarker == nullptr)
            CreateQuestMarker();

         else
            billboardSetQuestMarker->setVisible(true);

         // start update
         CLRString^ strTex = PREFIX_QUESTMARKER_TEXTURE +
            QuestMarkerColors::GetColorFor(RoomObject->Flags).ToString();

         CLRString^ strMat = PREFIX_QUESTMARKER_MATERIAL +
            QuestMarkerColors::GetColorFor(RoomObject->Flags).ToString();

         ::Ogre::String& texName = StringConvert::CLRToOgre(strTex);
         ::Ogre::String& matName = StringConvert::CLRToOgre(strMat);

         // create Texture and material
         TextureManager& texMan = TextureManager::getSingleton();

         if (!texMan.resourceExists(texName))
         {
            // create bitmap to draw on
            System::Drawing::Bitmap^ bitmap =
               Meridian59::Drawing2D::ImageComposerGDI<Data::Models::ObjectBase^>::QuestMarkerBitmap::Get(RoomObject);

            // create texture from bitmap
            Util::CreateTexture(bitmap, texName, TEXTUREGROUP_MOVABLETEXT);

            // cleanup
            delete bitmap;
         }

         Util::CreateMaterialQuestMarker(matName, texName, MATERIALGROUP_MOVABLETEXT);

         // set material
         billboardSetQuestMarker->setMaterialName(matName);

         // get size from texture
         TexturePtr texPtr = Ogre::static_pointer_cast<Ogre::Texture>(
            texMan.createOrRetrieve(texName, TEXTUREGROUP_MOVABLETEXT).first);

         questMarkerTextureWidth = (float)texPtr->getWidth();
         questMarkerTextureHeight = (float)texPtr->getHeight();

         billboardSetQuestMarker->setDefaultDimensions(questMarkerTextureWidth, questMarkerTextureHeight);
         billboardSetQuestMarker->setBounds(AxisAlignedBox::BOX_NULL, 0.0f);

         // update quest marker position
         UpdateQuestMarkerPosition();

         texPtr.reset();
      }

      else if (billboardSetQuestMarker != nullptr)
      {
         // hide it
         billboardSetQuestMarker->setVisible(false);
      }
   };

   void RemoteNode::UpdateCameraPosition()
   {
      ::Ogre::SceneNode* cameraNode = OgreClient::Singleton->CameraNode;

      // no cameranode or not attached to this scenenode
      if (!cameraNode || cameraNode->getParentSceneNode() != sceneNode)
         return;

      // get new height
      float height = Util::GetSceneNodeHeight(SceneNode) * 0.93f;

      // get old position
      const ::Ogre::Vector3& oldPos = cameraNode->getPosition();

      // get difference between old/new
      float diff = height - (float)oldPos.y;

      // ignore small differences, otherwise adjust camera to top of image
      if (abs(diff) > 16.0f)
         cameraNode->setPosition(0.0f, height, 0.0f);
   };

   void RemoteNode::UpdateQuestMarkerPosition()
   {
      if (sceneNode == nullptr || billboardSetQuestMarker == nullptr || billboardQuestMarker == nullptr)
         return;
      if (!(RoomObject->Flags->IsNPCActiveQuest || RoomObject->Flags->IsNPCHasQuests || RoomObject->Flags->IsMobKillQuest))
         return;

      CLRString^ strMat = PREFIX_QUESTMARKER_MATERIAL + 
         QuestMarkerColors::GetColorFor(RoomObject->Flags).ToString();

      ::Ogre::String& matName = StringConvert::CLRToOgre(strMat);

      // create Texture and material
      MaterialManager& matMan = MaterialManager::getSingleton();
      MaterialPtr matPtr = matMan.getByName(matName);

      if (!matPtr)
         return;

      Pass* pass = matPtr->getTechnique(0)->getPass(0);

      // get fragment shader parameters from ambient pass
      const GpuProgramParametersSharedPtr paramsPass =
         pass->getVertexProgramParameters();

      // determine height to place label
      float h = Util::GetSceneNodeHeight(sceneNode) + 3.0f;

      // get change
      float diff = h - lastQuestMarkerOffset;

      // ignore small changes (due to animations)
      if (abs(diff) > 16.0f)
      {
         paramsPass->setNamedConstant("offset", h);
         lastQuestMarkerOffset = h;
      }
   };

   void RemoteNode::UpdateNamePosition()
   {
      if (billboardSetName == nullptr || billboardName == nullptr || sceneNode == nullptr)
         return;

      CLRString^ strMat = PREFIX_NAMETEXT_MATERIAL + RoomObject->Name +
         "/" + NameColors::GetColorFor(RoomObject->Flags).ToString();

      ::Ogre::String& matName = StringConvert::CLRToOgre(strMat);

      // create Texture and material
      MaterialManager& matMan = MaterialManager::getSingleton();
      MaterialPtr matPtr = matMan.getByName(matName);

      if (!matPtr)
         return;

      Pass* pass = matPtr->getTechnique(0)->getPass(0);

      // get fragment shader parameters from ambient pass
      const GpuProgramParametersSharedPtr paramsPass =
         pass->getVertexProgramParameters();

      // determine height to place label
      float h = Util::GetSceneNodeHeight(sceneNode) + 1.0f;

      // get change
      float diff = h - lastNameOffset;
      
      // ignore small changes (due to animations)
      if (abs(diff) > 16.0f)
      {
         paramsPass->setNamedConstant("offset", h);
         lastNameOffset = h;
      }
   };

   void RemoteNode::RefreshOrientation()
   {
      // reset scenenode orientation to M59 angle
      Util::SetOrientationFromAngle(*SceneNode, (float)RoomObject->Angle);
   };

   void RemoteNode::RefreshPosition()
   {
      ::Ogre::Vector3& pos = Util::ToOgre(RoomObject->Position3D);

      // update scenenode position from datamodel
      SceneNode->setPosition(pos);

      // update sound if any
      if (sounds->size() > 0)
      {
         vec3df irrpos;
         irrpos.X = (ik_f32)pos.x;
         irrpos.Y = (ik_f32)pos.y;
         irrpos.Z = (ik_f32)-pos.z;

         ISound* sound;

         // update position of attached playback sounds
         for(std::list<ISound*>::iterator it=sounds->begin(); 
            it !=sounds->end(); 
            it++)
         {
            sound = *it;
            sound->setPosition(irrpos);
         }
      }
   };

   void RemoteNode::UpdateMaterial()
   {
   };

   void RemoteNode::SetVisible(bool Value)
   {
      if (SceneNode != nullptr)
      {
         // disable everything attached
         SceneNode->setVisible(Value);

         // reenable light
         if (light != nullptr)
            light->setVisible(true);

         // update name and quest marker
         UpdateName();
         UpdateQuestMarker();
      }
   }

   void RemoteNode::AddSound(ISound* Sound)
   {
      // add sound to list
      sounds->push_back(Sound);
   };
};};
