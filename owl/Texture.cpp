// ======================================================================== //
// Copyright 2020 Ingo Wald                                                 //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#include "Texture.h"
#include "Context.h"

namespace owl {

  size_t bytesPerTexel(OWLTexelFormat       texelFormat)
  {
    switch(texelFormat){
    case OWL_TEXEL_FORMAT_RGBA8:
      return sizeof(vec4uc);
    case OWL_TEXEL_FORMAT_RGBA32F:
      return sizeof(vec4f);
    case OWL_TEXEL_FORMAT_R32F:
      return sizeof(float);
    default:
      throw std::runtime_error("texel format not implemented");
    };
  }
  
  Texture::Texture(Context *const context,
                   vec2i                size,
                   uint32_t             linePitchInBytes,
                   OWLTexelFormat       texelFormat,
                   OWLTextureFilterMode filterMode,
                   OWLTextureWrapMode   wrapMode,
                   const void          *texels
                   )
    : RegisteredObject(context,context->textures)
  {
    assert(size.x > 0);
    assert(size.y > 0);
    int32_t pitch  = linePitchInBytes;

    if (pitch == 0)
      pitch = size.x*bytesPerTexel(texelFormat);//sizeof(vec4uc);

    assert(texels != nullptr);
    
    for (auto device : context->getDevices()) {
      SetActiveGPU forLifeTime(device);

      cudaResourceDesc res_desc = {};
      
      cudaChannelFormatDesc channel_desc;
      switch (texelFormat) {
      case OWL_TEXEL_FORMAT_RGBA8:
        channel_desc = cudaCreateChannelDesc<uchar4>();
        break;
      case OWL_TEXEL_FORMAT_RGBA32F:
        channel_desc = cudaCreateChannelDesc<float4>();
        break;
      case OWL_TEXEL_FORMAT_R32F:
        channel_desc = cudaCreateChannelDesc<float>();
        break;
      default:
        throw std::runtime_error("texel format not implemented");
      }

      cudaArray_t   pixelArray;
      CUDA_CALL(MallocArray(&pixelArray,
                             &channel_desc,
                             size.x,size.y));
      textureArrays.push_back(pixelArray);
      
      CUDA_CALL(Memcpy2DToArray(pixelArray,
                                 /* offset */0,0,
                                 texels,
                                 pitch,pitch,size.y,
                                 cudaMemcpyHostToDevice));
      
      res_desc.resType          = cudaResourceTypeArray;
      res_desc.res.array.array  = pixelArray;
      
      cudaTextureDesc tex_desc     = {};
      switch (wrapMode) {
      case OWL_TEXTURE_WRAP:
        tex_desc.addressMode[0]      = cudaAddressModeWrap;
        tex_desc.addressMode[1]      = cudaAddressModeWrap;
        break;
      case OWL_TEXTURE_CLAMP:
        tex_desc.addressMode[0]      = cudaAddressModeClamp;
        tex_desc.addressMode[1]      = cudaAddressModeClamp;
        break;
      default:
        throw std::runtime_error("not implemented wrapmode");
      };
      assert(filterMode == OWL_TEXTURE_NEAREST
             ||
             filterMode == OWL_TEXTURE_LINEAR);
      tex_desc.filterMode          =
        filterMode == OWL_TEXTURE_NEAREST
        ? cudaFilterModePoint
        : cudaFilterModeLinear;
      switch (texelFormat) {
      case OWL_TEXEL_FORMAT_RGBA8:
        tex_desc.readMode            = cudaReadModeNormalizedFloat;
        break;
      case OWL_TEXEL_FORMAT_RGBA32F:
        tex_desc.readMode            = cudaReadModeElementType;
        break;
      case OWL_TEXEL_FORMAT_R32F:
        tex_desc.readMode            = cudaReadModeElementType;
        break;
      default:
        throw std::runtime_error("texel format not implemented");
      }
      tex_desc.normalizedCoords    = 1;
      tex_desc.maxAnisotropy       = 1;
      tex_desc.maxMipmapLevelClamp = 99;
      tex_desc.minMipmapLevelClamp = 0;
      tex_desc.mipmapFilterMode    = cudaFilterModePoint;
      tex_desc.borderColor[0]      = 1.0f;
      tex_desc.sRGB                = 0;
      
      // Create texture object
      cudaTextureObject_t cuda_tex = 0;
      CUDA_CALL(CreateTextureObject(&cuda_tex, &res_desc, &tex_desc, nullptr));

      textureObjects.push_back(cuda_tex);
    }
  }

  Texture::~Texture()
  {
    destroy();
  }

  /*! destroy whatever resources this buffer's ll-layer handle this
    may refer to; this will not destruct the current object
    itself, but should already release all its references */
  void Texture::destroy()
  {
    if (ID < 0)
      /* already destroyed */
      return;

    for (auto device : context->getDevices()) {
      SetActiveGPU forLifeTime(device);
      uint32_t id = device->getCudaDeviceID();
      cudaDestroyTextureObject(textureObjects[id]);
      cudaFreeArray(textureArrays[id]);
    }

    deviceData.clear();
    
    registry.forget(this); // sets ID to -1
  }
  
} // ::owl
