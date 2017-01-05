#include "TopoBuilder.h"



//
// Initiates topology building from the file URL by first creating a media source, and then
// adding source and sink nodes for every stream found in the file.
//
HRESULT CTopoBuilder::RenderURL(PCWSTR fileUrl, HWND videoHwnd)
{
    HRESULT hr = S_OK;

    do
    {
        m_videoHwnd = videoHwnd;

        // first create the media source for the file/stream passed in.  Fail and fall out if
        // the media source creation fails (e.g. if the file format is not recognized)
        hr = CreateMediaSource(fileUrl);
        BREAK_ON_FAIL(hr);

        hr = CreateTopology();
    }
    while(false);

    return hr;
}

template <class T> void SafeRelease(T **ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = NULL;
	}
}

HRESULT CreateVideoDeviceSource(IMFMediaSource **ppSource)
{
    *ppSource = NULL;

    IMFMediaSource *pSource = NULL;
    IMFAttributes *pAttributes = NULL;
    IMFActivate **ppDevices = NULL;

    // Create an attribute store to specify the enumeration parameters.
    HRESULT hr = MFCreateAttributes(&pAttributes, 1);
    if (FAILED(hr))
    {
        goto done;
    }

    // Source type: video capture devices
    hr = pAttributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, 
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
        );
    if (FAILED(hr))
    {
        goto done;
    }

    // Enumerate devices.
    UINT32 count;
    hr = MFEnumDeviceSources(pAttributes, &ppDevices, &count);
    if (FAILED(hr))
    {
        goto done;
    }

    if (count == 0)
    {
        hr = E_FAIL;
        goto done;
    }

	
    // Create the media source object.
    hr = ppDevices[0]->ActivateObject(IID_PPV_ARGS(&pSource));
	TCHAR *name;
	UINT32 len;
	hr = ppDevices[0]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &len);

    if (FAILED(hr))
    {
        goto done;
    }

    *ppSource = pSource;
    (*ppSource)->AddRef();

done:
    SafeRelease(&pAttributes);

    for (DWORD i = 0; i < count; i++)
    {
        SafeRelease(&ppDevices[i]);
    }
    CoTaskMemFree(ppDevices);
    SafeRelease(&pSource);
    return hr;
}

//
// Create a media source for the specified URL string.  The URL can be a path to a stream, 
// or it can be a path to a local file.
//
HRESULT CTopoBuilder::CreateMediaSource(PCWSTR sURL)
{
    HRESULT hr = S_OK;
    MF_OBJECT_TYPE objectType = MF_OBJECT_INVALID;
    CComPtr<IMFSourceResolver> pSourceResolver;
    CComPtr<IUnknown> pSource;

	IMFMediaSource *ppSource;
	CreateVideoDeviceSource(&ppSource);
	m_pSource = ppSource;

	return hr;
}



//
// Since we created the source, we are responsible for shutting it down.
//
HRESULT CTopoBuilder::ShutdownSource(void)
{    
    HRESULT hr = S_OK;

    if(m_pSource != NULL)
    {
        // shut down the source
        hr = m_pSource->Shutdown();
        
        // release the source, since all subsequent calls to it will fail
        m_pSource.Release();
    }
    else
    {
        hr = E_UNEXPECTED;
    }

    return hr;
}



//
// Create a playback topology from the media source by extracting presentation
// and stream descriptors from the source, and creating a sink for each of them.
//
HRESULT CTopoBuilder::CreateTopology(void)
{
    HRESULT hr = S_OK;

    CComPtr<IMFPresentationDescriptor> pPresDescriptor;
    DWORD nSourceStreams = 0;

    do
    {
        // release the old topology if there was one        
        m_pTopology.Release();
        
        // Create a new topology.
        hr = MFCreateTopology(&m_pTopology);
        BREAK_ON_FAIL(hr);

        // Create the presentation descriptor for the media source - a container object that
        // holds a list of the streams and allows selection of streams that will be used.
        hr = m_pSource->CreatePresentationDescriptor(&pPresDescriptor);
        BREAK_ON_FAIL(hr);

        // Get the number of streams in the media source
        hr = pPresDescriptor->GetStreamDescriptorCount(&nSourceStreams);
        BREAK_ON_FAIL(hr);

        // For each stream, create source and sink nodes and add them to the topology.
        for (DWORD x = 0; x < nSourceStreams; x++)
        {
            hr = AddBranchToPartialTopology(pPresDescriptor, x);
            
            // if we failed to build a branch for this stream type, then deselect it
            // that will cause the stream to be disabled, and the source will not produce
            // any data for it
            if(FAILED(hr))
            {
                hr = pPresDescriptor->DeselectStream(x);
                BREAK_ON_FAIL(hr);
            }
        }
    }
    while(false);

    return hr;
}

#include <guiddef.h>


HRESULT CreateMFTransform(
	IMFStreamDescriptor *pSD,
	IMFTransform **ppDecoder
	)
{
	HRESULT hr = S_OK;
	UINT32 count = 0;

	IMFActivate **ppActivate = NULL;

	MFT_REGISTER_TYPE_INFO inInfo = { 0 };

	IMFMediaTypeHandler *pTH = NULL;
	IMFMediaType *pMT = NULL;
	GUID subtype;
	pSD->GetMediaTypeHandler(&pTH);
	pTH->GetCurrentMediaType(&pMT);
	pMT->GetGUID(MF_MT_SUBTYPE, &subtype);
	printf("%d",pMT);
	SafeRelease(&pTH);

	inInfo.guidMajorType = MFMediaType_Video;
	inInfo.guidSubtype = subtype;

	CLSID sda = { 0xb77014bf, 0x4ac, 0x4b0d, 0x90, 0xbd, 0x52, 0xca, 0x8a, 0xdf, 0x73, 0xed };
	IMFAttributes*            ppAttributes = NULL;
	LPWSTR name;
	MFTGetInfo(sda, &name, NULL, NULL, NULL, NULL, &ppAttributes);
	
	//UINT32 unFlags =	MFT_ENUM_FLAG_SYNCMFT  | 
	//					MFT_ENUM_FLAG_LOCALMFT | 
	//					MFT_ENUM_FLAG_SORTANDFILTER;
	UINT32 unFlags = MFT_ENUM_FLAG_ALL;
	hr = MFTEnumEx(
		MFT_CATEGORY_VIDEO_EFFECT,
		//MFT_CATEGORY_VIDEO_DECODER,
		MFT_ENUM_FLAG_SYNCMFT,
		NULL,
		NULL,
		&ppActivate,
		&count
		);

	if (SUCCEEDED(hr) && count == 0)
	{
		hr = MF_E_TOPO_CODEC_NOT_FOUND;
	}

	IMFTransform *pMFTransform = NULL;
	if (SUCCEEDED(hr))
	{
		hr = ppActivate[4]->ActivateObject(IID_PPV_ARGS(&pMFTransform));
	}

	//hr = pMFTransform->SetInputType(0, pMT, NULL);
	///*hr = pMT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	//hr = pMT->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_YUY2);*/
	//hr = pMFTransform->SetOutputType(0, pMT, NULL);

	//DWORD inMin, inMax, outMin, outMax,
	//	inCount, outCount;
	//	
	//hr = pMFTransform->GetStreamLimits(&inMin, &inMax, &outMin, &outMax);
	//hr = pMFTransform->GetStreamCount(&inCount, &outCount);
	//DWORD *inIDs = new DWORD[inCount], *outIDs = new DWORD[outCount];
	//hr = pMFTransform->GetStreamIDs(inCount, inIDs, outCount, outIDs);
	//
	//IMFMediaType *MFMT = NULL; 
	//int k = 0;
	//hr = S_OK;
	//while (SUCCEEDED(hr))
	//{
	//	hr = pMFTransform->GetOutputAvailableType(0, k, &MFMT);
	//	if(SUCCEEDED(hr))
	//		LogMediaType(MFMT);
	//	SafeRelease(&MFMT);
	//	k++;
	//}
	//k = 0;
	//hr = S_OK;
	//while (SUCCEEDED(hr))
	//{
	//	hr = pMFTransform->GetInputAvailableType(0, k, &MFMT);
	//	if(SUCCEEDED(hr))
	//		LogMediaType(MFMT);
	//	SafeRelease(&MFMT);
	//	k++;
	//}
	//
	//_MFT_INPUT_STREAM_INFO inMFInfo, outMFInfo;
	//hr = pMFTransform->GetInputStreamInfo(0, &inMFInfo);
	//hr = pMFTransform->GetInputStreamInfo(0, &outMFInfo);
	//
	//hr = pMFTransform->GetInputCurrentType(0, &pMT);
	//if(SUCCEEDED(hr))
	//	LogMediaType(pMT);
	//SafeRelease(&pMT);
	//hr = pMFTransform->GetOutputCurrentType(0, &pMT);
	//if(SUCCEEDED(hr))
	//	LogMediaType(pMT);
	//SafeRelease(&pMT);

	for (UINT32 i = 0; i < count; i++)
	{
		ppActivate[i]->Release();
	}
	CoTaskMemFree(ppActivate);

	if (SUCCEEDED(hr))
		*ppDecoder = pMFTransform;
	return hr;
}


HRESULT AddMFTNode(
	IMFTopology *pTopology,
	IMFTransform *pTransform
	)
{
	HRESULT hr = S_OK;


	// Create the node.

	return hr;
}

//
//  Adds a topology branch for one stream.
//
//  pPresDescriptor: The source's presentation descriptor.
//  nStream: Index of the stream to render.
//
//  For each stream, we must do the following steps:
//    1. Create a source node associated with the stream.
//    2. Create a sink node for the renderer.
//    3. Connect the two nodes.
//  The media session will resolve the topology, inserting intermediate decoder and other 
//  transform MFTs that will process the data in preparation for consumption by the 
// renderers.
//
HRESULT CTopoBuilder::AddBranchToPartialTopology(
    IMFPresentationDescriptor* pPresDescriptor, 
    DWORD nStream)
{
    HRESULT hr = S_OK;

    CComPtr<IMFStreamDescriptor> pStreamDescriptor;
    CComPtr<IMFTopologyNode> pSourceNode;
    CComPtr<IMFTopologyNode> pOutputNode;
    BOOL streamSelected = FALSE;

	IMFTopologyNode     *pNode = NULL;
	IMFTransform		*pImg = NULL;
	hr = MFCreateTopologyNode(MF_TOPOLOGY_TRANSFORM_NODE, &pNode);
	

    do
    {
        BREAK_ON_NULL(m_pTopology, E_UNEXPECTED);

        // Get the stream descriptor for this stream (information about stream).
        hr = pPresDescriptor->GetStreamDescriptorByIndex(nStream, &streamSelected, 
            &pStreamDescriptor);
        BREAK_ON_FAIL(hr);

        // Create the topology branch only if the stream is selected - IE if the user wants
        // to play it.
        if (streamSelected)
        {
            // Create a source node for this stream.
            hr = CreateSourceStreamNode(pPresDescriptor, pStreamDescriptor, pSourceNode);
            BREAK_ON_FAIL(hr);

            // Create the sink node for the renderer.
            hr = CreateOutputNode(pStreamDescriptor, m_videoHwnd, pOutputNode);
            BREAK_ON_FAIL(hr);


			hr = CreateMFTransform(pStreamDescriptor, &pImg);

			if (SUCCEEDED(hr))
			{
				hr = pNode->SetObject(pImg);
			}

			// Add the node to the topology.
			if (SUCCEEDED(hr))
			{
				hr = m_pTopology->AddNode(pNode);
			}

            // Add the source and sink nodes to the topology.
            hr = m_pTopology->AddNode(pSourceNode);
            BREAK_ON_FAIL(hr);

            hr = m_pTopology->AddNode(pOutputNode);
            BREAK_ON_FAIL(hr);

            // Connect the source node to the sink node.  The resolver will find the
            // intermediate nodes needed to convert media types.
			hr = pSourceNode->ConnectOutput(0, pNode, 0);
			hr = pNode->ConnectOutput(0, pOutputNode, 0);

        }
    }
    while(false);

    return hr;
}



//
//  Create a source node for the specified stream
//
//  pPresDescriptor: Presentation descriptor for the media source.
//  pStreamDescriptor: Stream descriptor for the stream.
//  pNode: Reference to a pointer to the new node - returns the new node.
//
HRESULT CTopoBuilder::CreateSourceStreamNode(
    IMFPresentationDescriptor* pPresDescriptor,
    IMFStreamDescriptor* pStreamDescriptor,
    CComPtr<IMFTopologyNode> &pNode)
{
    HRESULT hr = S_OK;

    do
    {
        BREAK_ON_NULL(pPresDescriptor, E_UNEXPECTED);
        BREAK_ON_NULL(pStreamDescriptor, E_UNEXPECTED);

        pNode = NULL;

        // Create the topology node, indicating that it must be a source node.
        hr = MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &pNode);
        BREAK_ON_FAIL(hr);

        // Associate the node with the source by passing in a pointer to the media source
        // and indicating that it is the source
        hr = pNode->SetUnknown(MF_TOPONODE_SOURCE, m_pSource);
        BREAK_ON_FAIL(hr);

        // Set the node presentation descriptor attribute of the node by passing 
        // in a pointer to the presentation descriptor
        hr = pNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, pPresDescriptor);
        BREAK_ON_FAIL(hr);

        // Set the node stream descriptor attribute by passing in a pointer to the stream
        // descriptor
        hr = pNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, pStreamDescriptor);
        BREAK_ON_FAIL(hr);
    }
    while(false);

    // if failed, clear the output parameter
    if(FAILED(hr))
        pNode = NULL;

    return hr;
}




//
//  This function creates an output node for a stream (sink).
//
HRESULT CTopoBuilder::CreateOutputNode(
    IMFStreamDescriptor* pStreamDescriptor,
    HWND hwndVideo,
    CComPtr<IMFTopologyNode> &pNode)
{
    HRESULT hr = S_OK;
    CComPtr<IMFMediaTypeHandler> pHandler;
    CComPtr<IMFActivate> pRendererActivate;

    GUID majorType = GUID_NULL;

    do
    {
        BREAK_ON_NULL(pStreamDescriptor, E_UNEXPECTED);

        // Get the media type handler for the stream, which will be used to process
        // the media types of the stream.  The handler stores the media type.
        hr = pStreamDescriptor->GetMediaTypeHandler(&pHandler);
        BREAK_ON_FAIL(hr);

        // Get the major media type (e.g. video or audio)
        hr = pHandler->GetMajorType(&majorType);
        BREAK_ON_FAIL(hr);

        // Create an IMFActivate controller object for the renderer, based on the media type
        // The activation objects are used by the session in order to create the renderers 
        // only when they are needed - i.e. only right before starting playback.  The 
        // activation objects are also used to shut down the renderers.
        if (majorType == MFMediaType_Audio)
        {
            // if the stream major type is audio, create the audio renderer.
            hr = MFCreateAudioRendererActivate(&pRendererActivate);
        }
        else if (majorType == MFMediaType_Video)
        {
            // if the stream major type is video, create the video renderer, passing in the
            // video window handle - that's where the video will be playing.
            hr = MFCreateVideoRendererActivate(hwndVideo, &pRendererActivate);
        }
        else
        {
            // fail if the stream type is not video or audio.  For example, fail
            // if we encounter a CC stream.
            hr = E_FAIL;
        }

        BREAK_ON_FAIL(hr);

        pNode = NULL;

        // Create the node that will represent the renderer
        hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &pNode);
        BREAK_ON_FAIL(hr);

        // Store the IActivate object in the sink node - it will be extracted later by the
        // media session during the topology render phase.
        hr = pNode->SetObject(pRendererActivate);
        BREAK_ON_FAIL(hr);
    }
    while(false);

    // if failed, clear the output parameter
    if(FAILED(hr))
        pNode = NULL;

    return hr;
}









