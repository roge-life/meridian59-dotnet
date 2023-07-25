#include "stdafx.h"

namespace Meridian59 { namespace Ogre 
{
   static ProjectileNode2D::ProjectileNode2D()
   {
   };

   ProjectileNode2D::ProjectileNode2D(Data::Models::Projectile^ Projectile, ::Ogre::SceneManager* SceneManager)
   {
      this->Projectile = Projectile;
      sceneManager     = SceneManager;
      hash             = gcnew Murmur3(0);

      Projectile->PropertyChanged += 
         gcnew PropertyChangedEventHandler(this, &ProjectileNode2D::OnProjectilePropertyChanged);

      Projectile->AppearanceChanged += 
         gcnew System::EventHandler(this, &ProjectileNode2D::OnProjectileAppearanceChanged);

      ::Ogre::String& ostr_id        = ::Ogre::StringConverter::toString(Projectile->ID);
      ::Ogre::String& ostr_billboard = PREFIX_PROJECTILE2D_BILLBOARD + ostr_id;
      ::Ogre::String& ostr_node      = PREFIX_PROJECTILE2D_SCENENODE + ostr_id;
      if (sceneManager->hasBillboardSet(ostr_billboard))
      {
	 billboardSet=sceneManager->getBillboardSet(ostr_billboard);
      }
      else
      {
      // create billboardset for 1 billboard
      billboardSet = sceneManager->createBillboardSet(ostr_billboard, 1);
      
      billboardSet->setBillboardOrigin(BillboardOrigin::BBO_BOTTOM_CENTER);
      billboardSet->setBillboardType(BillboardType::BBT_POINT);
      billboardSet->setAutoextend(false);

      // hide billboardset by default with no boundingbox (blank objects "something")
      billboardSet->setDefaultDimensions(0.0f, 0.0f);
      billboardSet->setBounds(AxisAlignedBox::BOX_NULL, 0.0f);
      billboardSet->setVisible(false);

      // create billboard to draw image on
      billboard = billboardSet->createBillboard(::Ogre::Vector3::ZERO);
      billboard->setColour(ColourValue::ZERO);
      }
      // create scenenode
      SceneNode = sceneManager->getRootSceneNode()->createChildSceneNode(ostr_node);
      SceneNode->attachObject(billboardSet);
#if DEBUG
      SceneNode->showBoundingBox(true);
#endif

      // possibly create attached light
      CreateLight();

      // update scenenode position from datamodel
      SceneNode->setPosition(Util::ToOgre(Projectile->Position3D));
   };

   ProjectileNode2D::~ProjectileNode2D()
   {
      // detach listener
      Projectile->PropertyChanged -= 
         gcnew PropertyChangedEventHandler(this, &ProjectileNode2D::OnProjectilePropertyChanged);

      Projectile->AppearanceChanged -= 
         gcnew System::EventHandler(this, &ProjectileNode2D::OnProjectileAppearanceChanged);

      // LIGHT FIRST! 
      DestroyLight();

      const ::Ogre::String& ostr_nodename = 
         PREFIX_PROJECTILE2D_SCENENODE + ::Ogre::StringConverter::toString(Projectile->ID);

      // cleanup scenenode
      if (sceneManager->hasSceneNode(ostr_nodename))
         sceneManager->destroySceneNode(ostr_nodename);

      if (billboardSet != nullptr)
      {
         billboardSet->detachFromParent();
         sceneManager->destroyBillboardSet(billboardSet);
      }

      billboard    = nullptr;
      billboardSet = nullptr;
      SceneNode    = nullptr;
   };

   void ProjectileNode2D::OnProjectilePropertyChanged(Object^ sender, PropertyChangedEventArgs^ e)
   {
      if (CLRString::Equals(e->PropertyName, Data::Models::Projectile::PROPNAME_POSITION3D))
      {
         // update scenenode position from datamodel
         SceneNode->setPosition(Util::ToOgre(Projectile->Position3D));
      }
   };

   void ProjectileNode2D::OnProjectileAppearanceChanged(Object^ sender, System::EventArgs^ e)
   {
      BgfBitmap^ bgfBmp = projectile->ViewerFrame;

      if (bgfBmp != nullptr)
      {
         // get a unique hash for the current appearance of the object
         hash->Reset(projectile->AppearanceHash);
         hash->Step(bgfBmp->Width);
         hash->Step(bgfBmp->Height);

         unsigned int key = hash->Finish();
            
         const ::Ogre::String& keystr  = ::Ogre::StringConverter::toString(key);
         const ::Ogre::String& texName = PREFIX_PROJECTILE2D_TEXTURE + keystr;
         const ::Ogre::String& matName = PREFIX_PROJECTILE2D_MATERIAL + keystr;

         // possibly create texture
         Util::CreateTextureA8R8G8B8(bgfBmp, texName, ::Ogre::String(TEXTUREGROUP_PROJECTILENODE2D), MIP_DEFAULT);

         // possibly create material
         Util::CreateMaterialObject(
            BASEMATERIAL, matName, texName,
            ::Ogre::String(MATERIALGROUP_PROJECTILENODE2D),
            &::Ogre::Vector4(1.0f, 1.0f, 1.0f, 1.0f));

         float scaledwidth = (float)bgfBmp->Width / (float)projectile->Resource->ShrinkFactor;
         float scaledheight = (float)bgfBmp->Height / (float)projectile->Resource->ShrinkFactor;
            
         float y = -bgfBmp->YOffset / (float)projectile->Resource->ShrinkFactor;
         billboard->setPosition(0.0f, y, 0.0f);

         // apply material and bbox
         billboardSet->setDefaultDimensions(scaledwidth, scaledheight);
         billboardSet->setMaterialName(matName);
         billboardSet->setVisible(true);
      }
   };

   void ProjectileNode2D::CreateLight()
   {
      ::Ogre::String& ostr_lightname = 
         PREFIX_PROJECTILE2D_LIGHT + ::Ogre::StringConverter::toString(Projectile->ID);

      // possibly create a mogre light
      Light = Util::CreateLight(Projectile, sceneManager,  ostr_lightname);

      if (Light != nullptr)
      {
         // maximum distance we render this light or skip it
         Light->setRenderingDistance(RemoteNode::MAXLIGHTRENDERDISTANCE);
         SceneNode->attachObject(Light);
         Light->setPosition(::Ogre::Vector3(0, 100, 0));
      }
   };

   void ProjectileNode2D::UpdateLight()
   {
      // adjust the light from M59 values (light class extension)
      if (light)
         Util::UpdateFromILightOwner(*light, Projectile);
   };

   void ProjectileNode2D::DestroyLight()
   {
      const ::Ogre::String& ostr_lightname = 
         PREFIX_PROJECTILE2D_LIGHT + ::Ogre::StringConverter::toString(Projectile->ID);

      if (sceneManager->hasLight(ostr_lightname))
         sceneManager->destroyLight(ostr_lightname);

      light = nullptr;
   };
};};
