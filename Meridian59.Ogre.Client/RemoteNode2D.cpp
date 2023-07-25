#include "stdafx.h"

namespace Meridian59 { namespace Ogre 
{
   RemoteNode2D::RemoteNode2D(Data::Models::RoomObject^ RoomObject, ::Ogre::SceneManager* SceneManager)
      : RemoteNode(RoomObject, SceneManager)
   {
      ::Ogre::String& ostr_name = 
         PREFIX_REMOTENODE2D_BILLBOARD + ::Ogre::StringConverter::toString(roomObject->ID);
      if (sceneManager->hasBillboardSet(ostr_name))
      {
	      billboardSetName=sceneManager->getBillboardSet(ostr_name);
         billboardSetName->clear();
         billboardSetName->detachFromParent();
         SceneManager->destroyBillboardSet(billboardSetName);
      }
      // create billboardset for 1 billboard
      billboardSet = SceneManager->createBillboardSet(ostr_name, 1);
      billboardSet->setBillboardType(BillboardType::BBT_ORIENTED_SELF);
      billboardSet->setAutoextend(false);
      billboardSet->setUseAccurateFacing(true);

      // note: IsHanging overlaps with some playertypes
      // workaround: must not have set IsPlayer also
      billboardSet->setBillboardOrigin(
#ifdef VANILLA
         roomObject->Flags->IsHanging && !roomObject->Flags->IsPlayer ?
#else
         roomObject->Flags->IsHanging ?
#endif
         BillboardOrigin::BBO_TOP_CENTER : 
         BillboardOrigin::BBO_BOTTOM_CENTER
      );

      // hide billboardset by default with no boundingbox (blank objects "something")
      billboardSet->setDefaultDimensions(0.0f, 0.0f);
      billboardSet->setBounds(AxisAlignedBox::BOX_NULL, 0.0f);

      // create billboard to draw image on
      billboard = billboardSet->createBillboard(::Ogre::Vector3::ZERO);
      billboard->setColour(ColourValue::ZERO);
      billboard->mDirection = ::Ogre::Vector3::UNIT_Y;

      // attach to scenenode
      if (!billboardSet->isAttached())
        SceneNode->attachObject(billboardSet);

      // possibly create attached light
      CreateLight();

      // the imagecomposer providing composed images of the object
      imageComposer = gcnew ImageComposerOgre<Data::Models::RoomObject^>();
      imageComposer->ApplyYOffset = true;
      imageComposer->IsScalePow2 = true;
      imageComposer->UseViewerFrame = true;
      imageComposer->NewImageAvailable += gcnew System::EventHandler(this, &RemoteNode2D::OnNewImageAvailable);

      // initial position and orientation
      RefreshPosition();
      RefreshOrientation();

      // set imagecomposer source, this will trigger image creation
      imageComposer->DataSource = RoomObject;

      // update name position, this needs created image!
      UpdateNamePosition();
      UpdateQuestMarkerPosition();
   };

   RemoteNode2D::~RemoteNode2D()
   {
      if (imageComposer)
      {
         // detach listener
         imageComposer->NewImageAvailable -= 
            gcnew System::EventHandler(this, &RemoteNode2D::OnNewImageAvailable);

         delete imageComposer;
      }

      if (billboardSet)
      {
         billboardSet->clear();
         billboardSet->detachFromParent();
         SceneManager->destroyBillboardSet(billboardSet);
      }

      imageComposer  = nullptr;
      billboard      = nullptr;
      billboardSet   = nullptr;
   };

   void RemoteNode2D::OnNewImageAvailable(Object^ sender, System::EventArgs^ e)
   {
      // get object size in world size
      float scaledwidth = (float)imageComposer->RenderInfo->WorldSize.X;
      float scaledheight = (float)imageComposer->RenderInfo->WorldSize.Y;

      // set size of billboardset
      billboardSet->setDefaultDimensions(scaledwidth, scaledheight);

      // UV coords
      billboard->setTexcoordRect(
         imageComposer->RenderInfo->UVStart.X, 
         imageComposer->RenderInfo->UVStart.Y, 
         imageComposer->RenderInfo->UVEnd.X, 
         imageComposer->RenderInfo->UVEnd.Y);

      // update material
      UpdateMaterial();

      // approximated bbox
      AxisAlignedBox bbBox;

#ifdef VANILLA
      if (roomObject->Flags->IsHanging && !roomObject->Flags->IsPlayer)
#else
      if (roomObject->Flags->IsHanging)
#endif
      {
         bbBox = AxisAlignedBox(
            ::Ogre::Vector3(-scaledwidth / 2.0f, -scaledheight, -scaledwidth / 2.0f), 
            ::Ogre::Vector3(scaledwidth / 2.0f, 0, scaledwidth / 2.0f));
      }
      else
      {
         bbBox = AxisAlignedBox(
            ::Ogre::Vector3(-scaledwidth / 2.0f, 0, -scaledwidth / 2.0f), 
            ::Ogre::Vector3(scaledwidth / 2.0f, scaledheight, scaledwidth / 2.0f));
      }

      // set bbox
      billboardSet->setBounds(bbBox, 0.0f);

      // update possible attached name label and quest marker
      UpdateNamePosition();
      UpdateQuestMarkerPosition();

      // update camera position (might be different now)
      UpdateCameraPosition();
   };

   void RemoteNode2D::CreateLight()
   {
      RemoteNode::CreateLight();

      // place the light at half object height
      if (Light != nullptr)
         Light->setPosition(::Ogre::Vector3(0.0f, 50.0f, 0.0f));
   };

   void RemoteNode2D::RefreshPosition()
   {
      RemoteNode::RefreshPosition();

      if (!billboardSet)
         return;

      // note: IsHanging overlaps with some playertypes
      // workaround: must not have set IsPlayer also
      billboardSet->setBillboardOrigin(
#ifdef VANILLA
         roomObject->Flags->IsHanging && !roomObject->Flags->IsPlayer ?
#else
         roomObject->Flags->IsHanging ?
#endif
         BillboardOrigin::BBO_TOP_CENTER : 
         BillboardOrigin::BBO_BOTTOM_CENTER
         );
   };

   void RemoteNode2D::UpdateMaterial()
      {
      // INVISIBLE
      if (RoomObject->Flags->Drawing == ObjectFlags::DrawingType::Invisible)
         billboardSet->setMaterialName(*imageComposer->Image->MaterialNameInvisible);

      // BLACK (e.g. shadowform)
      else if (RoomObject->Flags->Drawing == ObjectFlags::DrawingType::Black)
         billboardSet->setMaterialName(*imageComposer->Image->MaterialNameBlack);

      // TARGET
      else if (RoomObject->IsTarget)
         billboardSet->setMaterialName(*imageComposer->Image->MaterialNameTarget);

      // MOUSEOVER
      else if (RoomObject->IsHighlighted)
         billboardSet->setMaterialName(*imageComposer->Image->MaterialNameMouseOver);

      // FLASHING
      else if (RoomObject->Flags->IsFlashing)
         billboardSet->setMaterialName(*imageComposer->Image->MaterialNameFlashing);

      // TRANSLUCENT
      // 75%
      else if (RoomObject->Flags->Drawing == ObjectFlags::DrawingType::Translucent75)
         billboardSet->setMaterialName(*imageComposer->Image->MaterialNameTranslucent75);

      // 50%
      else if (RoomObject->Flags->Drawing == ObjectFlags::DrawingType::Translucent50)
         billboardSet->setMaterialName(*imageComposer->Image->MaterialNameTranslucent50);

      // 25%
      else if (RoomObject->Flags->Drawing == ObjectFlags::DrawingType::Translucent25)
         billboardSet->setMaterialName(*imageComposer->Image->MaterialNameTranslucent25);

      // DITHERINVIS (e.g. logoff ghost)
      else if (RoomObject->Flags->Drawing == ObjectFlags::DrawingType::DitherInvis)
         billboardSet->setMaterialName(*imageComposer->Image->MaterialNameTranslucent50);

      // DITHERTRANS
      else if (RoomObject->Flags->Drawing == ObjectFlags::DrawingType::DitherTrans)
         billboardSet->setMaterialName(*imageComposer->Image->MaterialNameTranslucent50);

      // DEFAULT
      else
         billboardSet->setMaterialName(*imageComposer->Image->MaterialNameDefault);
   };
};};
