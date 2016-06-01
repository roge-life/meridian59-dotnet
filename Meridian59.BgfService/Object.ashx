﻿<%@ WebHandler Language="C#" Class="Object" %>

using System;
using System.Web;
using System.Collections.Specialized;
using System.Web.Routing;

using System.Drawing;
using System.Drawing.Imaging;
using System.Drawing.Drawing2D;

using Meridian59.Common;
using Meridian59.Common.Constants;
using Meridian59.Data.Models;
using Meridian59.Drawing2D;
using Meridian59.Files.BGF;

public class Object : IHttpHandler 
{
    static Object()
    {
        ImageComposerGDI<ObjectBase>.IsCacheEnabled = false;
        ImageComposerGDI<ObjectBase>.InterpolationMode = InterpolationMode.HighQualityBicubic;        
    }
    
    public void ProcessRequest (HttpContext context)
    {
        BgfFile bgfFile;

        // -------------------------------------------------------       
        // read basic and mainoverlay parameters from url-path (see Global.asax):
        //  object/{width}/{height}/{scale}/{file}/{group}/{angle}/{palette}

        RouteValueDictionary parms = context.Request.RequestContext.RouteData.Values;

        string parmFile = parms.ContainsKey("file") ? (string)parms["file"] : null;
        string parmGroup = parms.ContainsKey("group") ? (string)parms["group"] : null;
        string parmPalette = parms.ContainsKey("palette") ? (string)parms["palette"] : null;
        string parmAngle = parms.ContainsKey("angle") ? (string)parms["angle"] : null;
        
        // -------------------------------------------------------
        // verify minimum parameters exist

        if (String.IsNullOrEmpty(parmFile))
        {
            context.Response.StatusCode = 404;
            context.Response.End();
            return;
        }

        // --------------------------------------------------
        // try to get the main BGF from cache or load from disk

        if (!Cache.GetBGF(parmFile, out bgfFile))
        {
            context.Response.StatusCode = 404;
            context.Response.End();
            return;
        }
       
        // --------------------------------------------------
        // try to parse other params
        
        ushort group = 0;
        byte paletteidx = 0;
        ushort angle = 0;
        
        UInt16.TryParse(parmGroup, out group);
        Byte.TryParse(parmPalette, out paletteidx);
        UInt16.TryParse(parmAngle, out angle);
        
        // remove full periods from angle
        angle %= GeometryConstants.MAXANGLE;

        // map 0 and negative groups to first group
        // group is 1-based ...
        if (group < 1)
            group = 1;
        
        // requested group out of range
        if (group > bgfFile.FrameSets.Count)
        {
            context.Response.StatusCode = 404;
            context.Response.End();
            return;
        }

        // --------------------------------------------------
        // try get the frame

        BgfBitmap bgfBmp = bgfFile.GetFrame(group, angle);
        if (bgfBmp == null)
        {
            context.Response.StatusCode = 404;
            context.Response.End();
            return;
        }

        // --------------------------------------------------
        // create gameobject

        ObjectBase gameObject = new ObjectBase();
        gameObject.Resource = bgfFile;
        gameObject.ColorTranslation = paletteidx;
        gameObject.Animation = new AnimationNone(group);
        gameObject.ViewerAngle = angle;
        
        // -------------------------------------------------------       
        // read suboverlay array params from query parameters:
        //  object/..../?subov={file};{group};{palette};{hotspot}&subov=...

        string[] parmSubOverlays = context.Request.Params.GetValues("subov");

        if (parmSubOverlays != null)
        {
            foreach(string s in parmSubOverlays)
            {
                string[] subOvParms = s.Split(';');

                if (subOvParms == null || subOvParms.Length < 4)
                    continue;

                BgfFile bgfSubOv;
                string subOvFile = subOvParms[0];                
                if (!Cache.GetBGF(subOvFile, out bgfSubOv))
                    continue;
                              
                ushort subOvGroup;
                byte subOvPalette;
                byte subOvHotspot;
                
                if (!ushort.TryParse(subOvParms[1], out subOvGroup) ||
                    !byte.TryParse(subOvParms[2], out subOvPalette) ||
                    !byte.TryParse(subOvParms[3], out subOvHotspot))
                {
                    continue;
                }

                // map 0 and negative groups to first group
                // group is 1-based ...
                if (subOvGroup < 1)
                    subOvGroup = 1;
        
                // create suboverlay
                SubOverlay subOv = new SubOverlay(
                    0, new AnimationNone(subOvGroup), subOvHotspot, subOvPalette, 0);

                // set bgf resource
                subOv.Resource = bgfSubOv;
                
                // add to gameobject's suboverlays
                gameObject.SubOverlays.Add(subOv);                
            }
        }
            
        // --------------------------------------------------
        // create composed image
     
        ImageComposerGDI<ObjectBase> imageComposer = new ImageComposerGDI<ObjectBase>();

        gameObject.Tick(0, 1);
        imageComposer.DataSource = gameObject;
        
        if (imageComposer.Image == null)
        {
            context.Response.StatusCode = 404;
            context.Response.End();
            return;
        }
      
        // --------------------------------------------------
        // write the response (encode to png)

        context.Response.ContentType = "image/png";
        imageComposer.Image.Save(context.Response.OutputStream, ImageFormat.Png);
        context.Response.Flush();
        context.Response.End();
        
        imageComposer.Image.Dispose();
    }
 
    public bool IsReusable 
    {
        get 
        {
            return false;
        }
    }

}