#pragma once

#include "Common.h"

// Media Foundation headers
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <evr.h>



//
//  The CTopoBuilder class wraps constructs the playback topology.
//
class CTopoBuilder
{
    public:
        CTopoBuilder(void)  {};
        ~CTopoBuilder(void) { ShutdownSource(); };

        // create a topology for the URL that will be rendered in the specified window
        HRESULT RenderURL(PCWSTR sURL, HWND videoHwnd);

        // get the created topology
        IMFTopology* GetTopology(void) { return m_pTopology; }

        // shutdown the media source for the topology
        HRESULT ShutdownSource(void);

    private:
        CComQIPtr<IMFTopology> m_pTopology;                 // the topology itself
        CComQIPtr<IMFMediaSource> m_pSource;                // the MF source
        CComQIPtr<IMFVideoDisplayControl> m_pVideoDisplay;  // the EVR
        HWND m_videoHwnd;                                   // the target window

        HRESULT CreateMediaSource(PCWSTR sURL);
        HRESULT CreateTopology(void);

        HRESULT AddBranchToPartialTopology(
            IMFPresentationDescriptor* pPresDescriptor,
            DWORD iStream);

        HRESULT CreateSourceStreamNode(
            IMFPresentationDescriptor* pPresDescr,
            IMFStreamDescriptor* pStreamDescr,
            CComPtr<IMFTopologyNode> &ppNode);
    
        HRESULT CreateOutputNode(
            IMFStreamDescriptor* pStreamDescr,
            HWND hwndVideo, 
            CComPtr<IMFTopologyNode> &pNode);
};

