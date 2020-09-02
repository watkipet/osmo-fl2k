#include <SoapySDR/Device.hpp>
#include <SoapySDR/Logger.h>
#include <SoapySDR/Types.h>
#include <thread>
#include <osmo-fl2k.h>

typedef enum fl2kTXFormat
{
    FL2K_TX_FORMAT_FLOAT32,
    FL2K_TX_FORMAT_INT16,
    FL2K_TX_FORMAT_INT8,
    FL2K_TX_FORMAT_UINT16,
    FL2K_TX_FORMAT_UINT8
} fl2kTXFormat;

#define DEFAULT_NUM_BUFFERS 4
#define BYTES_PER_SAMPLE 1

// TODO: Is this correct?
#define DEFAULT_BUFFER_LENGTH FL2K_XFER_LEN

/***********************************************************************
 * Device interface
 **********************************************************************/
class SoapyOsmoFL2K : public SoapySDR::Device
{
public:
    SoapyOsmoFL2K(const SoapySDR::Kwargs &args);
    
    ~SoapyOsmoFL2K(void);

    /*******************************************************************
     * Identification API
     ******************************************************************/

    std::string getDriverKey(void) const;

    std::string getHardwareKey(void) const;

    SoapySDR::Kwargs getHardwareInfo(void) const;

    /*******************************************************************
     * Channels API
     ******************************************************************/

    size_t getNumChannels(const int) const;
    

    /*******************************************************************
     * Stream API
     ******************************************************************/

    std::vector<std::string> getStreamFormats(const int direction, const size_t channel) const;

    std::string getNativeStreamFormat(const int direction, const size_t channel, double &fullScale) const;

    SoapySDR::ArgInfoList getStreamArgsInfo(const int direction, const size_t channel) const;

    SoapySDR::Stream *setupStream(const int direction, const std::string &format, const std::vector<size_t> &channels =
            std::vector<size_t>(), const SoapySDR::Kwargs &args = SoapySDR::Kwargs());

    void closeStream(SoapySDR::Stream *stream);

    size_t getStreamMTU(SoapySDR::Stream *stream) const;

    int activateStream(
            SoapySDR::Stream *stream,
            const int flags = 0,
            const long long timeNs = 0,
            const size_t numElems = 0);

    int deactivateStream(SoapySDR::Stream *stream, const int flags = 0, const long long timeNs = 0);

    int writeStream(
            SoapySDR::Stream *stream,
            const void * const *buffs,
            const size_t numElems,
            int &flags,
            const long long timeNs,
            const long timeoutUs = 100000);
    
    int readStreamStatus(
        SoapySDR::Stream *stream,
        size_t &chanMask,
        int &flags,
        long long &timeNs,
        const long timeoutUs = 100000);

    /*******************************************************************
     * Direct buffer access API
     ******************************************************************/

    size_t getNumDirectAccessBuffers(SoapySDR::Stream *stream);

    int getDirectAccessBufferAddrs(SoapySDR::Stream *stream, const size_t handle, void **buffs);

    int acquireWriteBuffer(
        SoapySDR::Stream *stream,
        size_t &handle,
        void **buffs,
        const long timeoutUs = 100000);

    void releaseWriteBuffer(
        SoapySDR::Stream *stream,
        const size_t handle,
        const size_t numElems,
        int &flags,
        const long long timeNs = 0);

    /*******************************************************************
     * Antenna API
     ******************************************************************/

    std::vector<std::string> listAntennas(const int direction, const size_t channel) const;

    void setAntenna(const int direction, const size_t channel, const std::string &name);

    std::string getAntenna(const int direction, const size_t channel) const;
    
    /*******************************************************************
     * Frontend corrections API
     ******************************************************************/

    bool hasDCOffsetMode(const int direction, const size_t channel) const;

    bool hasFrequencyCorrection(const int direction, const size_t channel) const;

    /*******************************************************************
     * Sample Rate API
     ******************************************************************/

    void setSampleRate(const int direction, const size_t channel, const double rate);

    double getSampleRate(const int direction, const size_t channel) const;

    std::vector<double> listSampleRates(const int direction, const size_t channel) const;

    void setBandwidth(const int direction, const size_t channel, const double bw);

    double getBandwidth(const int direction, const size_t channel) const;

    std::vector<double> listBandwidths(const int direction, const size_t channel) const;
    
    /*******************************************************************
     * Time API
     ******************************************************************/

    std::vector<std::string> listTimeSources(void) const;

    std::string getTimeSource(void) const;

    bool hasHardwareTime(const std::string &what = "") const;

    long long getHardwareTime(const std::string &what = "") const;

    void setHardwareTime(const long long timeNs, const std::string &what = "");

    /*******************************************************************
     * Settings API
     ******************************************************************/

    SoapySDR::ArgInfoList getSettingInfo(void) const;

    void writeSetting(const std::string &key, const std::string &value);

    std::string readSetting(const std::string &key) const;
    
    /*******************************************************************
     * Settings API
     ******************************************************************/
    
    static const std::string &fl2kErrorToString(enum fl2k_error error);
    
    static const std::string &fl2kErrorToString(int error);

private:

    //device handle
    int deviceId;
    fl2k_dev_t *dev;
    
    //cached settings
    fl2kTXFormat txFormat;
    uint32_t sampleRate;
    size_t bufferLength, asyncBuffs;
    std::atomic<long long> ticks;
    bool _signed;
    std::chrono::system_clock::time_point start;
    
public:
    struct Buffer
    {
        unsigned long long tick;
        // TODO: Make unsigned char
        signed char data[FL2K_BUF_LEN];
    };

    //async api usage
    void tx_callback(fl2k_data_info_t *data_info);

    std::mutex _buf_mutex;
    std::condition_variable _buf_cond;

    std::vector<Buffer> _buffs;
    size_t    _buf_head;
    size_t    _buf_tail;
    std::atomic<ssize_t>	_buf_count;
    signed char *_currentBuff;
    std::atomic<bool> _underflowEvent;
    size_t _currentHandle;
    size_t bufferedElems;
    long long bufTicks;
    std::atomic<bool> resetBuffer;

    static int fl2k_count;
    static std::vector<SoapySDR::Kwargs> fl2k_devices;
};
