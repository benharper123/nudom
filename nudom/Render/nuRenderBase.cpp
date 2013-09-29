#include "pch.h"
#include "nuRenderBase.h"

nuRenderBase::nuRenderBase()
{
	TexIDOffset = 0;
}

void nuRenderBase::SurfaceLost_ForgetTextures()
{
	TexIDOffset++;
	if ( TexIDOffset > nuGlobal()->MaxTextureID )
		TexIDOffset = 0;
	TexIDToNative.clear();
}

bool nuRenderBase::IsTextureValid( nuTextureID texID ) const
{
	nuTextureID relativeID = texID - TEX_OFFSET_ONE - TexIDOffset;
	return relativeID < (nuTextureID) TexIDToNative.size();
}

nuTextureID nuRenderBase::RegisterTexture( void* deviceTexID )
{
	nuTextureID maxTexID = nuGlobal()->MaxTextureID;
	nuTextureID id = TexIDOffset + (nuTextureID) TexIDToNative.size();
	if ( id > maxTexID )
		id -= maxTexID;

	TexIDToNative += deviceTexID;
	return id + TEX_OFFSET_ONE;
}

void* nuRenderBase::GetTextureDeviceID( nuTextureID texID ) const
{
	nuTextureID absolute = texID - TEX_OFFSET_ONE - TexIDOffset;
	if ( absolute >= (nuTextureID) TexIDToNative.size() )
	{
		NUPANIC( "nuRenderBase::GetTextureDeviceID: Invalid texture ID" );
		return NULL;
	}
	return TexIDToNative[absolute];
}