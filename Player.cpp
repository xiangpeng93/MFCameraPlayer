
#include "Player.h"



//
//  CPlayer constructor - instantiates internal objects and initializes MF
//
CPlayer::CPlayer(HWND videoWindow, HRESULT* pHr) :
    m_pSession(NULL),
    m_hwndVideo(videoWindow),
    m_state(PlayerState_Closed),
    m_nRefCount(1)
{
    HRESULT hr = S_OK;

    do
    {
        // initialize COM
        hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        BREAK_ON_FAIL(hr);
    
        // Start up Media Foundation platform.
        hr = MFStartup(MF_VERSION);
        BREAK_ON_FAIL(hr);

        // create an event that will be fired when the asynchronous IMFMediaSession::Close() 
        // operation is complete
        m_closeCompleteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        BREAK_ON_NULL(m_closeCompleteEvent, E_UNEXPECTED);
    }
    while(false);

    *pHr = hr;
}



CPlayer::~CPlayer(void)
{
    CloseSession();

    // Shutdown the Media Foundation platform
    MFShutdown();

    // uninitialize COM
    CoUninitialize();

    // close the event
    CloseHandle(m_closeCompleteEvent);
}


//
// Receive asynchronous event.
//
HRESULT CPlayer::Invoke(IMFAsyncResult* pAsyncResult)
{
    CComPtr<IMFMediaEvent> pEvent;
    HRESULT hr = S_OK;

    do
    {
        CComCritSecLock<CComAutoCriticalSection> lock(m_critSec);

        BREAK_ON_NULL(pAsyncResult, E_UNEXPECTED);

        // Get the event from the event queue.
        hr = m_pSession->EndGetEvent(pAsyncResult, &pEvent);
        BREAK_ON_FAIL(hr);

        // If the player is not closing, process the media event - if it is, do nothing.
        if (m_state != PlayerState_Closing)
        {
            hr = ProcessMediaEvent(pEvent);
            BREAK_ON_FAIL(hr);
        }

        // If the media event is MESessionClosed, it is guaranteed to be the last event.  If
        // the event is MESessionClosed, ProcessMediaEvent() will return S_FALSE.  In that 
        // case do not request the next event - otherwise tell the media session that this 
        // player is the object that will handle the next event in the queue.
        if(hr != S_FALSE)
        {
            hr = m_pSession->BeginGetEvent(this, NULL);
            BREAK_ON_FAIL(hr);
        }
    }
    while(false);

    return S_OK;
}



//
//  Called by Invoke() to do the actual event processing, and determine what, if anything,
//  needs to be done.  Returns S_FALSE if the media event type is MESessionClosed.
//
HRESULT CPlayer::ProcessMediaEvent(CComPtr<IMFMediaEvent>& pMediaEvent)
{
    HRESULT hrStatus = S_OK;            // Event status
    HRESULT hr = S_OK;
    UINT32 TopoStatus = MF_TOPOSTATUS_INVALID; 
    MediaEventType eventType;

    do
    {
        BREAK_ON_NULL( pMediaEvent, E_POINTER );

        // Get the event type.
        hr = pMediaEvent->GetType(&eventType);
        BREAK_ON_FAIL(hr);

        // Get the event status. If the operation that triggered the event did
        // not succeed, the status is a failure code.
        hr = pMediaEvent->GetStatus(&hrStatus);
        BREAK_ON_FAIL(hr);

        // Check if the async operation succeeded.
        if (FAILED(hrStatus))
        {
            hr = hrStatus;
            break;
        }

        // Switch on the event type. Update the internal state of the CPlayer as needed.
        if(eventType == MESessionTopologyStatus)
        {
            // Get the status code.
            hr = pMediaEvent->GetUINT32(MF_EVENT_TOPOLOGY_STATUS, (UINT32*)&TopoStatus);
            BREAK_ON_FAIL(hr);

            if (TopoStatus == MF_TOPOSTATUS_READY)
            {
                m_state = PlayerState_Stopped;

                hr = OnTopologyReady();
            }
        }
        else if(eventType == MEEndOfPresentation)
        {
            m_state = PlayerState_Stopped;
        }
        else if (eventType == MESessionClosed)
        {
            // signal to anybody listening that the session is closed
            SetEvent(m_closeCompleteEvent);
            hr = S_FALSE;
        }
    }
    while(false);

    return hr;
}




//
// IUnknown methods
//
HRESULT CPlayer::QueryInterface(REFIID riid, void** ppv)
{
    HRESULT hr = S_OK;

    if(ppv == NULL)
    {
        return E_POINTER;
    }

    if(riid == __uuidof(IMFAsyncCallback))
    {
        *ppv = static_cast<IMFAsyncCallback*>(this);
    }
    else if(riid == __uuidof(IUnknown))
    {
        *ppv = static_cast<IUnknown*>(this);
    }
    else
    {
        *ppv = NULL;
        hr = E_NOINTERFACE;
    }

    if(SUCCEEDED(hr))
        AddRef();

    return hr;
}

ULONG CPlayer::AddRef(void)
{
    return InterlockedIncrement(&m_nRefCount);
}

ULONG CPlayer::Release(void)
{
    ULONG uCount = InterlockedDecrement(&m_nRefCount);
    if (uCount == 0)
    {
        delete this;
    }
    return uCount;
}



//
// OpenURL is the main initialization function that triggers bulding of the core
// MF components.
//
HRESULT CPlayer::OpenURL(PCWSTR sURL)
{
    CComPtr<IMFTopology> pTopology = NULL;
    HRESULT hr = S_OK;

    do
    {
        CComCritSecLock<CComAutoCriticalSection> lock(m_critSec);

        // create a media session if one doesn't exist already
        if(m_pSession == NULL)
        {
            hr = CreateSession();
            BREAK_ON_FAIL(hr);
        }

        // build the topology.  Here we are using the TopoBuilder helper class.
        hr = m_topoBuilder.RenderURL(sURL, m_hwndVideo);
        BREAK_ON_FAIL(hr);

        // get the topology from the TopoBuilder
        pTopology = m_topoBuilder.GetTopology();
        BREAK_ON_NULL(pTopology, E_UNEXPECTED);

        // add the topology to the internal queue of topologies associated with this session
        hr = m_pSession->SetTopology(0, pTopology);
        BREAK_ON_FAIL(hr);
        
        // If a brand new topology was just created, set the player state to "open pending"
        // - not playing yet, but ready to begin.
        if(m_state == PlayerState_Ready)
        {
            m_state = PlayerState_OpenPending;
        }
    }
    while(false);

    if (FAILED(hr))
    {
        m_state = PlayerState_Closed;
    }

    return hr;
}


//
//  Starts playback from paused or stopped state.
//
HRESULT CPlayer::Play(void)
{
    HRESULT hr = S_OK;

    do
    {
        CComCritSecLock<CComAutoCriticalSection> lock(m_critSec);

        // make sure everything is in the right state
        if (m_state != PlayerState_Paused && m_state != PlayerState_Stopped)
        {
            hr = MF_E_INVALIDREQUEST;
            break;
        }
    
        // make sure the session has been created
        BREAK_ON_NULL(m_pSession, E_UNEXPECTED);

        // start playback
        hr = StartPlayback();
        BREAK_ON_FAIL(hr);

        // if we got here, everything was properly started
        m_state = PlayerState_Started;
    }
    while(false);

    return hr;
}


//
//  Pauses playback.
//
HRESULT CPlayer::Pause(void)
{
    HRESULT hr = S_OK;

    do
    {
        CComCritSecLock<CComAutoCriticalSection> lock(m_critSec);

        // pause makes sense only if playback has started
        if (m_state != PlayerState_Started)
        {
            hr = MF_E_INVALIDREQUEST;
            break;
        }

        // make sure the session has been created
        BREAK_ON_NULL(m_pSession, E_UNEXPECTED);

        // pause
        hr = m_pSession->Pause();
        BREAK_ON_FAIL(hr);

        // if we got here, everything is properly paused
        m_state = PlayerState_Paused;
    }
    while(false);

    return hr;
}

//
//  Repaints the video window - called from main windows message loop when WM_PAINT
// is received.
//
HRESULT CPlayer::Repaint(void)
{
    HRESULT hr = S_OK;

    if (m_pVideoDisplay)
    {
        hr = m_pVideoDisplay->RepaintVideo();
    }

    return hr;
}






//
// Handler for MESessionTopologyReady event - starts video playback.
//
HRESULT CPlayer::OnTopologyReady(void)
{
    HRESULT hr = S_OK;

    do
    {
        // release any previous instance of the m_pVideoDisplay interface
        m_pVideoDisplay.Release();

        // Ask the session for the IMFVideoDisplayControl interface. This interface is 
        // implemented by the EVR (Enhanced Video Renderer) and is exposed by the media 
        // session as a service.  The session will query the topology for the right 
        // component and return this EVR interface.  The interface will be used to tell the
        // video to repaint whenever the hosting window receives a WM_PAINT window message.
        hr = MFGetService(m_pSession, MR_VIDEO_RENDER_SERVICE,  IID_IMFVideoDisplayControl,
                (void**)&m_pVideoDisplay);
        BREAK_ON_FAIL(hr);

        // since the topology is ready, start playback
        hr = Play();
    }
    while(false);

    return hr;
}



//
//  Creates a new instance of the media session.
//
HRESULT CPlayer::CreateSession(void)
{
    HRESULT hr = S_OK;
    
    do
    {
        CComCritSecLock<CComAutoCriticalSection> lock(m_critSec);

        // close the session if one is already created
        hr = CloseSession();
        BREAK_ON_FAIL(hr);

        if(m_state != PlayerState_Closed)
        {
            hr = E_UNEXPECTED;
            break;
        }

        // Create the media session.
        hr = MFCreateMediaSession(NULL, &m_pSession);
        BREAK_ON_FAIL(hr);
        BREAK_ON_NULL(m_pSession, E_UNEXPECTED);

        m_state = PlayerState_Ready;

        // designate this class as the one that will be handling events from the media 
        // session
        hr = m_pSession->BeginGetEvent((IMFAsyncCallback*)this, NULL);
        BREAK_ON_FAIL(hr);
    }
    while(false);

    return hr;
}


//
//  Closes the media session, blocking until the session closure is complete
//
HRESULT CPlayer::CloseSession(void)
{
    HRESULT hr = S_OK;
    DWORD dwWaitResult = 0;

    do
    {
        CComCritSecLock<CComAutoCriticalSection> lock(m_critSec);

        m_state = PlayerState_Closing;

        // release the video display object
        m_pVideoDisplay = NULL;

        // Call the asynchronous Close() method and then wait for the close
        // operation to complete on another thread
        if (m_pSession != NULL)
        {
            m_state = PlayerState_Closing;

            hr = m_pSession->Close();
            
            // IMFMediaSession::Close() may return MF_E_SHUTDOWN if the session is already
            // shut down. That's expected and acceptable.
            if (SUCCEEDED(hr))
            {
                // Begin waiting for the Win32 close event, fired in CPlayer::Invoke(). The 
                // close event will indicate that the close operation is finished, and the 
                // session can be shut down.
                dwWaitResult = WaitForSingleObject(m_closeCompleteEvent, 5000);
                if (dwWaitResult == WAIT_TIMEOUT)
                {
                    hr = E_UNEXPECTED;
                    break;
                }
            }
        }

        // Shut down the media session. (Synchronous operation, no events.)  Releases all of
        // the internal session resources.
        if (m_pSession != NULL)
        {
            m_pSession->Shutdown();
        }

        // release the session
        m_pSession = NULL;

        m_state = PlayerState_Closed;
    }
    while(false);

    return hr;
}


//
//  Start playback from the current position.
//
HRESULT CPlayer::StartPlayback(void)
{
    HRESULT hr = S_OK;
    PROPVARIANT varStart;

    do
    {
        BREAK_ON_NULL(m_pSession, E_UNEXPECTED);

        PropVariantInit(&varStart);
        varStart.vt = VT_EMPTY;

        // If Start fails later, we will get an MESessionStarted event with an error code, 
        // and will update our state. Passing in GUID_NULL and VT_EMPTY indicates that
        // playback should start from the current position.
        hr = m_pSession->Start(&GUID_NULL, &varStart);

        PropVariantClear(&varStart);
    }
    while(false);

    return hr;
}
