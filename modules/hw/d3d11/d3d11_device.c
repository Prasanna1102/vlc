/*****************************************************************************
 * d3d11_device.c : D3D11 decoder device from external ID3D11DeviceContext
 *****************************************************************************
 * Copyright © 2019 VLC authors, VideoLAN and VideoLabs
 *
 * Authors: Steve Lhomme <robux4@ycbcr.xyz>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_codec.h>

#include <vlc/libvlc.h>
#include <vlc/libvlc_picture.h>
#include <vlc/libvlc_media.h>
#include <vlc/libvlc_renderer_discoverer.h>
#include <vlc/libvlc_media_player.h>

#define COBJMACROS
#include <d3d11.h>
#include "d3d11_filters.h"

typedef struct {
    d3d11_handle_t         hd3d;

    struct {
        void                            *opaque;
        libvlc_video_output_cleanup_cb  cleanupDeviceCb;
    } external;

    d3d11_decoder_device_t                    dec_device;
} d3d11_decoder_device;

static void D3D11CloseDecoderDevice(vlc_decoder_device *device)
{
    d3d11_decoder_device *sys = device->sys;

    D3D11_ReleaseDevice(&sys->dec_device.d3d_dev);

    D3D11_Destroy(&sys->hd3d);

    if ( sys->external.cleanupDeviceCb )
        sys->external.cleanupDeviceCb( sys->external.opaque );

    vlc_obj_free( VLC_OBJECT(device), sys );
}

static const struct vlc_decoder_device_operations d3d11_dev_ops = {
    .close = D3D11CloseDecoderDevice,
};

static int D3D11OpenDecoderDevice(vlc_decoder_device *device, bool forced, vout_window_t *wnd)
{
    VLC_UNUSED(wnd);
    d3d11_decoder_device *sys = vlc_obj_malloc(VLC_OBJECT(device), sizeof(*sys));
    if (unlikely(sys==NULL))
        return VLC_ENOMEM;

    int ret = D3D11_Create(device, &sys->hd3d);
    if (ret != VLC_SUCCESS)
        return ret;

    sys->external.cleanupDeviceCb = NULL;
    HRESULT hr;
#if VLC_WINSTORE_APP
    /* LEGACY, the d3dcontext and swapchain were given by the host app */
    ID3D11DeviceContext *d3dcontext = (ID3D11DeviceContext*)(uintptr_t) var_InheritInteger(device, "winrt-d3dcontext");
    if ( likely(d3dcontext != NULL) )
    {
        hr = D3D11_CreateDeviceExternal(device, d3dcontext, true, &sys->dec_device.d3d_dev);
    }
    else
#endif
    {
        libvlc_video_output_setup_cb setupDeviceCb = var_InheritAddress( device, "vout-cb-setup" );
        if ( setupDeviceCb )
        {
            /* decoder device coming from the external app */
            sys->external.opaque          = var_InheritAddress( device, "vout-cb-opaque" );
            sys->external.cleanupDeviceCb = var_InheritAddress( device, "vout-cb-cleanup" );
            libvlc_video_setup_device_cfg_t cfg = {
                .hardware_decoding = true, /* always favor hardware decoding */
            };
            libvlc_video_setup_device_info_t out = { .d3d11.device_context = NULL };
            if (!setupDeviceCb( &sys->external.opaque, &cfg, &out ))
            {
                if (sys->external.cleanupDeviceCb)
                    sys->external.cleanupDeviceCb( sys->external.opaque );
                goto error;
            }
            hr = D3D11_CreateDeviceExternal(device, out.d3d11.device_context, true, &sys->dec_device.d3d_dev);
        }
        else
        {
            /* internal decoder device */
            hr = D3D11_CreateDevice( device, &sys->hd3d, NULL,
                                     true /* is_d3d11_opaque(chroma) */,
                                     forced, &sys->dec_device.d3d_dev );
        }
    }

    if ( FAILED( hr ) )
        goto error;

    device->ops = &d3d11_dev_ops;
    device->opaque = &sys->dec_device;
    device->type = VLC_DECODER_DEVICE_D3D11VA;
    device->sys = sys;

    return VLC_SUCCESS;
error:
    D3D11_Destroy(&sys->hd3d);
    vlc_obj_free( VLC_OBJECT(device), sys );
    return VLC_EGENERIC;
}

int D3D11OpenDecoderDeviceW8(vlc_decoder_device *device, vout_window_t *wnd)
{
    return D3D11OpenDecoderDevice(device, false, wnd);
}

int D3D11OpenDecoderDeviceAny(vlc_decoder_device *device, vout_window_t *wnd)
{
    return D3D11OpenDecoderDevice(device, true, wnd);
}
