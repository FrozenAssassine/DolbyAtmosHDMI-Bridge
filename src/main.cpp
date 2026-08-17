#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <SpatialAudioClient.h>
#include <wrl/client.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <algorithm>

#pragma comment(lib, "Ole32.lib")

using Microsoft::WRL::ComPtr;

// Supported Layout Definitions
struct AudioLayout
{
    std::string name;
    int numChannels;
    AudioObjectType staticMask;
    std::vector<AudioObjectType> targets;
    std::vector<std::string> channelNames;
};

// Available Layout Profiles
const std::vector<AudioLayout> LAYOUTS = {
    {"5.1.2 (Default - Top Front Heights)",
     8,
     AudioObjectType_FrontLeft | AudioObjectType_FrontRight | AudioObjectType_FrontCenter |
         AudioObjectType_LowFrequency | AudioObjectType_SideLeft | AudioObjectType_SideRight |
         AudioObjectType_TopFrontLeft | AudioObjectType_TopFrontRight,
     {AudioObjectType_FrontLeft, AudioObjectType_FrontRight, AudioObjectType_FrontCenter,
      AudioObjectType_LowFrequency, AudioObjectType_SideLeft, AudioObjectType_SideRight,
      AudioObjectType_TopFrontLeft, AudioObjectType_TopFrontRight},
     {"L", "R", "C", "LFE", "Ls", "Rs", "TFL", "TFR"}},
    {"5.1.4 (4 Heights: Top Front & Top Back)",
     10,
     AudioObjectType_FrontLeft | AudioObjectType_FrontRight | AudioObjectType_FrontCenter |
         AudioObjectType_LowFrequency | AudioObjectType_SideLeft | AudioObjectType_SideRight |
         AudioObjectType_TopFrontLeft | AudioObjectType_TopFrontRight |
         AudioObjectType_TopBackLeft | AudioObjectType_TopBackRight,
     {AudioObjectType_FrontLeft, AudioObjectType_FrontRight, AudioObjectType_FrontCenter,
      AudioObjectType_LowFrequency, AudioObjectType_SideLeft, AudioObjectType_SideRight,
      AudioObjectType_TopFrontLeft, AudioObjectType_TopFrontRight,
      AudioObjectType_TopBackLeft, AudioObjectType_TopBackRight},
     {"L", "R", "C", "LFE", "Ls", "Rs", "TFL", "TFR", "TBL", "TBR"}},
    {"7.1.2 (7.1 Surround + 2 Heights)",
     10,
     AudioObjectType_FrontLeft | AudioObjectType_FrontRight | AudioObjectType_FrontCenter |
         AudioObjectType_LowFrequency | AudioObjectType_SideLeft | AudioObjectType_SideRight |
         AudioObjectType_BackLeft | AudioObjectType_BackRight |
         AudioObjectType_TopFrontLeft | AudioObjectType_TopFrontRight,
     {AudioObjectType_FrontLeft, AudioObjectType_FrontRight, AudioObjectType_FrontCenter,
      AudioObjectType_LowFrequency, AudioObjectType_SideLeft, AudioObjectType_SideRight,
      AudioObjectType_BackLeft, AudioObjectType_BackRight,
      AudioObjectType_TopFrontLeft, AudioObjectType_TopFrontRight},
     {"L", "R", "C", "LFE", "Ls", "Rs", "BL", "BR", "TFL", "TFR"}},
    {"7.1.4 (Full 12-Channel Immersive Setup)",
     12,
     AudioObjectType_FrontLeft | AudioObjectType_FrontRight | AudioObjectType_FrontCenter |
         AudioObjectType_LowFrequency | AudioObjectType_SideLeft | AudioObjectType_SideRight |
         AudioObjectType_BackLeft | AudioObjectType_BackRight |
         AudioObjectType_TopFrontLeft | AudioObjectType_TopFrontRight |
         AudioObjectType_TopBackLeft | AudioObjectType_TopBackRight,
     {AudioObjectType_FrontLeft, AudioObjectType_FrontRight, AudioObjectType_FrontCenter,
      AudioObjectType_LowFrequency, AudioObjectType_SideLeft, AudioObjectType_SideRight,
      AudioObjectType_BackLeft, AudioObjectType_BackRight,
      AudioObjectType_TopFrontLeft, AudioObjectType_TopFrontRight,
      AudioObjectType_TopBackLeft, AudioObjectType_TopBackRight},
     {"L", "R", "C", "LFE", "Ls", "Rs", "BL", "BR", "TFL", "TFR", "TBL", "TBR"}}};

// Lock-Free SPSC Dynamic-Channel Ring Buffer
class DynamicAudioRingBuffer
{
private:
    static constexpr size_t CAPACITY = 32768;
    static constexpr size_t MASK = CAPACITY - 1;

    std::vector<std::vector<float>> m_channels;
    int m_numChannels;
    alignas(64) std::atomic<size_t> m_writeIndex{0};
    alignas(64) std::atomic<size_t> m_readIndex{0};

public:
    DynamicAudioRingBuffer(int numChannels) : m_numChannels(numChannels)
    {
        m_channels.resize(numChannels);
        for (int i = 0; i < numChannels; ++i)
        {
            m_channels[i].resize(CAPACITY, 0.0f);
        }
    }

    size_t AvailableRead() const
    {
        size_t w = m_writeIndex.load(std::memory_order_acquire);
        size_t r = m_readIndex.load(std::memory_order_relaxed);
        return (w >= r) ? (w - r) : (CAPACITY - (r - w));
    }

    size_t AvailableWrite() const
    {
        return (CAPACITY - 1) - AvailableRead();
    }

    void WriteFrames(const float *const *channelData, size_t numFrames)
    {
        size_t w = m_writeIndex.load(std::memory_order_relaxed);
        for (size_t f = 0; f < numFrames; ++f)
        {
            size_t idx = (w + f) & MASK;
            for (int ch = 0; ch < m_numChannels; ++ch)
            {
                m_channels[ch][idx] = channelData[ch][f];
            }
        }
        m_writeIndex.store((w + numFrames) & MASK, std::memory_order_release);
    }

    void ReadFrames(int channel, float *outBuffer, size_t numFrames)
    {
        size_t r = m_readIndex.load(std::memory_order_relaxed);
        for (size_t f = 0; f < numFrames; ++f)
        {
            outBuffer[f] = m_channels[channel][(r + f) & MASK];
        }
    }

    void AdvanceRead(size_t numFrames)
    {
        size_t r = m_readIndex.load(std::memory_order_relaxed);
        m_readIndex.store((r + numFrames) & MASK, std::memory_order_release);
    }
};

static std::atomic<bool> g_running{true};

BOOL WINAPI ConsoleHandler(DWORD signal)
{
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT)
    {
        std::cout << "\n[INFO] Stopping bridge...\n";
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

HRESULT FindDeviceByName(IMMDeviceEnumerator *devEnum, const std::wstring &nameSubstr, IMMDevice **ppDevice)
{
    ComPtr<IMMDeviceCollection> collection;
    HRESULT hr = devEnum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr))
        return hr;

    UINT count = 0;
    collection->GetCount(&count);

    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> dev;
        collection->Item(i, &dev);

        ComPtr<IPropertyStore> props;
        dev->OpenPropertyStore(STGM_READ, &props);

        PROPVARIANT varName;
        PropVariantInit(&varName);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &varName)))
        {
            std::wstring devName = varName.pwszVal ? varName.pwszVal : L"";
            PropVariantClear(&varName);

            if (devName.find(nameSubstr) != std::wstring::npos)
            {
                *ppDevice = dev.Detach();
                return S_OK;
            }
        }
    }
    return E_NOTFOUND;
}

int main()
{
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    std::cout << "=================================================\n";
    std::cout << "  Spatial Audio Multi-Layout Atmos Bridge        \n";
    std::cout << "=================================================\n\n";

    // 1. Layout Selection Menu
    std::cout << "Select Speaker Configuration:\n";
    for (size_t i = 0; i < LAYOUTS.size(); ++i)
    {
        std::cout << "  [" << (i + 1) << "] " << LAYOUTS[i].name << " (" << LAYOUTS[i].numChannels << " Channels)\n";
    }
    std::cout << "\nEnter choice [1-" << LAYOUTS.size() << "] (Default: 1): ";

    std::string input;
    std::getline(std::cin, input);
    int choice = 1;
    if (!input.empty())
    {
        try
        {
            choice = std::stoi(input);
            if (choice < 1 || choice > (int)LAYOUTS.size())
                choice = 1;
        }
        catch (...)
        {
            choice = 1;
        }
    }

    const AudioLayout &activeLayout = LAYOUTS[choice - 1];
    std::cout << "\n[OK] Selected Layout: " << activeLayout.name << "\n";
    std::cout << "     Channel Order  : ";
    for (size_t i = 0; i < activeLayout.channelNames.size(); ++i)
    {
        std::cout << (i + 1) << ":" << activeLayout.channelNames[i] << (i + 1 < activeLayout.channelNames.size() ? ", " : "\n\n");
    }

    ComPtr<IMMDeviceEnumerator> devEnum;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&devEnum));
    if (FAILED(hr))
        return -1;

    // 2. Locate Virtual Cable
    ComPtr<IMMDevice> captureDevice;
    hr = FindDeviceByName(devEnum.Get(), L"CABLE Input", &captureDevice);
    if (FAILED(hr))
        hr = FindDeviceByName(devEnum.Get(), L"CABLE", &captureDevice);
    if (FAILED(hr))
    {
        std::cerr << "[ERROR] Virtual Audio Cable device not found!\n";
        return -1;
    }
    std::cout << "[OK] Input Source : VB-Audio Cable\n";

    // 3. Locate AVR / HDMI Output
    ComPtr<IMMDevice> renderDevice;
    hr = FindDeviceByName(devEnum.Get(), L"DENON", &renderDevice);
    if (FAILED(hr))
    {
        devEnum->GetDefaultAudioEndpoint(eRender, eMultimedia, &renderDevice);
    }
    std::cout << "[OK] Output Target: Spatial Audio (Dolby Atmos for Home Theater)\n";

    // 4. Configure WASAPI Loopback Capture
    ComPtr<IAudioClient> captureClient;
    captureDevice->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr, (void **)&captureClient);

    WAVEFORMATEX *captureFormat = nullptr;
    captureClient->GetMixFormat(&captureFormat);

    bool isFloat = (captureFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    if (captureFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        WAVEFORMATEXTENSIBLE *pExt = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(captureFormat);
        if (pExt->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
            isFloat = true;
    }

    HANDLE captureEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    hr = captureClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        1000000, 0, captureFormat, nullptr);
    if (FAILED(hr))
    {
        std::cerr << "[ERROR] Failed to initialize WASAPI capture.\n";
        return -1;
    }
    captureClient->SetEventHandle(captureEvent);

    ComPtr<IAudioCaptureClient> captureReader;
    captureClient->GetService(IID_PPV_ARGS(&captureReader));
    captureClient->Start();

    // 5. Configure ISpatialAudioClient
    ComPtr<ISpatialAudioClient> spatialClient;
    hr = renderDevice->Activate(__uuidof(ISpatialAudioClient), CLSCTX_INPROC_SERVER, nullptr, (void **)&spatialClient);
    if (FAILED(hr))
    {
        std::cerr << "[ERROR] Dolby Atmos for Home Theater must be active in Windows!\n";
        return -1;
    }

    WAVEFORMATEXTENSIBLE wfx = {};
    wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfx.Format.nChannels = 1;
    wfx.Format.nSamplesPerSec = 48000;
    wfx.Format.wBitsPerSample = 32;
    wfx.Format.nBlockAlign = 4;
    wfx.Format.nAvgBytesPerSec = 48000 * 4;
    wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wfx.Samples.wValidBitsPerSample = 32;
    wfx.dwChannelMask = SPEAKER_FRONT_CENTER;
    wfx.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    HANDLE renderBufferEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    SpatialAudioObjectRenderStreamActivationParams params = {};
    params.ObjectFormat = reinterpret_cast<WAVEFORMATEX *>(&wfx);
    params.StaticObjectTypeMask = activeLayout.staticMask;
    params.MinDynamicObjectCount = 0;
    params.MaxDynamicObjectCount = 0;
    params.Category = AudioCategory_SoundEffects;
    params.EventHandle = renderBufferEvent;

    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_BLOB;
    var.blob.cbSize = sizeof(params);
    var.blob.pBlobData = reinterpret_cast<BYTE *>(&params);

    ComPtr<ISpatialAudioObjectRenderStream> spatialStream;
    hr = spatialClient->ActivateSpatialAudioStream(&var, IID_PPV_ARGS(&spatialStream));
    if (FAILED(hr))
    {
        std::cerr << "[ERROR] ActivateSpatialAudioStream failed.\n";
        return -1;
    }
    spatialStream->Start();

    DynamicAudioRingBuffer ringBuffer(activeLayout.numChannels);
    UINT32 captureChannels = captureFormat->nChannels;
    WORD bitsPerSample = captureFormat->wBitsPerSample;

    // 6. Background Capture Thread
    std::thread captureThread([&]()
                              {
        std::vector<std::vector<float>> tempChannels(activeLayout.numChannels);
        for (int ch = 0; ch < activeLayout.numChannels; ++ch) tempChannels[ch].resize(4096);
        std::vector<float*> tempPtrs(activeLayout.numChannels);
        for (int ch = 0; ch < activeLayout.numChannels; ++ch) tempPtrs[ch] = tempChannels[ch].data();

        while (g_running) {
            WaitForSingleObject(captureEvent, 100);

            UINT32 packetLength = 0;
            while (SUCCEEDED(captureReader->GetNextPacketSize(&packetLength)) && packetLength > 0) {
                BYTE* captureData = nullptr;
                UINT32 numFramesRead = 0;
                DWORD flags = 0;

                hr = captureReader->GetBuffer(&captureData, &numFramesRead, &flags, nullptr, nullptr);
                if (SUCCEEDED(hr) && captureData && numFramesRead > 0) {
                    if (tempChannels[0].size() < numFramesRead) {
                        for (int ch = 0; ch < activeLayout.numChannels; ++ch) {
                            tempChannels[ch].resize(numFramesRead);
                            tempPtrs[ch] = tempChannels[ch].data();
                        }
                    }

                    bool isSilent = (flags & AUDCLNT_BUFFERFLAGS_SILENT);

                    for (UINT32 s = 0; s < numFramesRead; ++s) {
                        for (int ch = 0; ch < activeLayout.numChannels; ++ch) {
                            if (isSilent || ch >= (int)captureChannels) {
                                tempChannels[ch][s] = 0.0f;
                            }
                            else if (isFloat) {
                                tempChannels[ch][s] = reinterpret_cast<float*>(captureData)[s * captureChannels + ch];
                            }
                            else if (bitsPerSample == 16) {
                                int16_t val = reinterpret_cast<int16_t*>(captureData)[s * captureChannels + ch];
                                tempChannels[ch][s] = static_cast<float>(val) / 32768.0f;
                            }
                            else if (bitsPerSample == 24) {
                                BYTE* samplePtr = &captureData[(s * captureChannels + ch) * 3];
                                int32_t val = (samplePtr[0] << 8) | (samplePtr[1] << 16) | (samplePtr[2] << 24);
                                tempChannels[ch][s] = static_cast<float>(val) / 2147483648.0f;
                            }
                            else if (bitsPerSample == 32) {
                                int32_t val = reinterpret_cast<int32_t*>(captureData)[s * captureChannels + ch];
                                tempChannels[ch][s] = static_cast<float>(val) / 2147483648.0f;
                            }
                            else {
                                tempChannels[ch][s] = 0.0f;
                            }
                        }
                    }

                    size_t writeRoom = ringBuffer.AvailableWrite();
                    size_t framesToWrite = (std::min)((size_t)numFramesRead, writeRoom);
                    if (framesToWrite > 0) {
                        ringBuffer.WriteFrames(tempPtrs.data(), framesToWrite);
                    }

                    captureReader->ReleaseBuffer(numFramesRead);
                }
            }
        } });

    std::vector<ComPtr<ISpatialAudioObject>> objects(activeLayout.numChannels);
    std::cout << "[RUNNING] Spatial Audio Bridge is active! Press Ctrl+C to exit.\n\n";

    // 7. Render Loop
    while (g_running)
    {
        WaitForSingleObject(renderBufferEvent, 100);
        if (!g_running)
            break;

        UINT32 availableObjects = 0, frameCountPerPass = 0;
        spatialStream->BeginUpdatingAudioObjects(&availableObjects, &frameCountPerPass);

        for (int ch = 0; ch < activeLayout.numChannels; ++ch)
        {
            if (!objects[ch])
            {
                spatialStream->ActivateSpatialAudioObject(activeLayout.targets[ch], &objects[ch]);
            }
        }

        size_t availableSamples = ringBuffer.AvailableRead();
        UINT32 targetSamples = 0;

        for (int ch = 0; ch < activeLayout.numChannels; ++ch)
        {
            if (objects[ch])
            {
                BYTE *outBuffer = nullptr;
                UINT32 outByteLen = 0;
                objects[ch]->GetBuffer(&outBuffer, &outByteLen);

                if (outBuffer && outByteLen > 0)
                {
                    float *outFloat = reinterpret_cast<float *>(outBuffer);
                    targetSamples = outByteLen / sizeof(float);

                    size_t samplesToRead = (std::min)((size_t)targetSamples, availableSamples);
                    if (samplesToRead > 0)
                    {
                        ringBuffer.ReadFrames(ch, outFloat, samplesToRead);
                    }

                    for (size_t s = samplesToRead; s < targetSamples; ++s)
                    {
                        outFloat[s] = 0.0f;
                    }
                }
            }
        }

        if (targetSamples > 0)
        {
            size_t consumed = (std::min)((size_t)targetSamples, availableSamples);
            ringBuffer.AdvanceRead(consumed);
        }

        spatialStream->EndUpdatingAudioObjects();
    }

    if (captureThread.joinable())
        captureThread.join();

    if (spatialStream)
        spatialStream->Stop();
    if (captureClient)
        captureClient->Stop();

    CoTaskMemFree(captureFormat);
    CloseHandle(captureEvent);
    CloseHandle(renderBufferEvent);
    CoUninitialize();

    std::cout << "[DONE] Stopped successfully.\n";
    return 0;
}