#include "SoapyOsmoFL2K.hpp"
#include <SoapySDR/Time.hpp>

#include <SoapySDR/Device.hpp>
#include <SoapySDR/Registry.hpp>
#include <osmo-fl2k.h>
#include <vector>

/***********************************************************************
 * Device interface
 **********************************************************************/
SoapyOsmoFL2K::SoapyOsmoFL2K(const SoapySDR::Kwargs &args)
{
    if (!SoapyOsmoFL2K::fl2k_count)
    {
        throw std::runtime_error("Osmo-FL2K device not found.");
    }

    deviceId = -1;
    dev = NULL;

    txFormat = FL2K_TX_FORMAT_FLOAT32;

    sampleRate = 2048000;

    asyncBuffs = DEFAULT_NUM_BUFFERS;
    bufferLength = DEFAULT_BUFFER_LENGTH;

    ticks = 0;

    bufferedElems = 0;
    resetBuffer = false;

    if (args.count("fl2k") != 0)
    {
        try
        {
            deviceId = std::stoi(args.at("fl2k"));
        }
        catch (const std::invalid_argument &)
        {
        }
        if (deviceId < 0 || deviceId >= SoapyOsmoFL2K::fl2k_count)
        {
            throw std::runtime_error(
                    "device index 'fl2k' out of range [0 .. " + std::to_string(SoapyOsmoFL2K::fl2k_count) + "].");
        }

        SoapySDR_logf(SOAPY_SDR_DEBUG, "Found Osmo-FL2K Device using device index parameter 'fl2k' = %d", deviceId);
    }
    else if (args.count("label") != 0)
    {
        std::string labelFind = args.at("label");
        for (int i = 0; i < SoapyOsmoFL2K::fl2k_count; i++)
        {
            SoapySDR::Kwargs devInfo = SoapyOsmoFL2K::fl2k_devices[i];
            if (devInfo.at("label") == labelFind)
            {
                SoapySDR_logf(SOAPY_SDR_DEBUG, "Found Osmo-FL2K Device #%d by name: %s", devInfo.at("label").c_str());
                deviceId = i;
                break;
            }
        }
    }

    if (deviceId == -1)
    {
        throw std::runtime_error("Unable to find requested Osmo-FL2K device.");
    }

    SoapySDR_logf(SOAPY_SDR_DEBUG, "Osmo-FL2K opening device %d", deviceId);

    fl2k_open(&dev, deviceId);
}

SoapyOsmoFL2K::~SoapyOsmoFL2K(void)
{
    //cleanup device handles
    fl2k_close(dev);
}

// Identification API

std::string SoapyOsmoFL2K::getDriverKey(void) const
{
    return "OSMOFL2K";
}

std::string SoapyOsmoFL2K::getHardwareKey(void) const
{
	return "UNKNOWN";
}

SoapySDR::Kwargs SoapyOsmoFL2K::getHardwareInfo(void) const
{
    //key/value pairs for any useful information
    //this also gets printed in --probe
    SoapySDR::Kwargs args;

    args["origin"] = "https://github.com/watkipet/SoapyOsmoFL2K";
    args["fl2k"] = std::to_string(deviceId);

    return args;
}

/*******************************************************************
 * Channels API
 ******************************************************************/

size_t SoapyOsmoFL2K::getNumChannels(const int dir) const
{
    return (dir == SOAPY_SDR_TX) ? 3 : 0;
}


SoapySDR::Kwargs SoapyOsmoFL2K::getChannelInfo(const int direction, const size_t channel) const
{
    if (direction != SOAPY_SDR_TX)
    {
        throw std::runtime_error("getChannelInfo failed: Osmo-FL2K only supports TX");
    }
    
    //key/value pairs for any useful information
    //this also gets printed in --probe
    SoapySDR::Kwargs args;
    
    std::string name;
    switch (channel)
    {
        case 0: name = "red"; break;
        case 1: name = "green"; break;
        case 2: name = "blue"; break;
        default:
            throw std::runtime_error("getChannelInfo failed: channel must be less than 3.");
    }

    args["name"] = name;

    return args;
}

bool SoapyOsmoFL2K::getFullDuplex(const int direction, const size_t channel) const
{
    if (direction != SOAPY_SDR_TX)
    {
        throw std::runtime_error("getFullDuplex failed: Osmo-FL2K only supports TX");
    }
    
    // None of the channels are full-duplex--just TX.
    return false;
}


/*******************************************************************
 * Antenna API
 ******************************************************************/

std::vector<std::string> SoapyOsmoFL2K::listAntennas(const int direction, const size_t channel) const
{
    // TODO: Should we change this to 3?
    if (direction != SOAPY_SDR_TX)
    {
        throw std::runtime_error("listAntennas failed: Osmo-FL2K only supports TX");
    }
    
    std::vector<std::string> antennas;
    switch (channel)
    {
        case 0: antennas.push_back("red"); break;
        case 1: antennas.push_back("green"); break;
        case 2: antennas.push_back("blue"); break;
        default:
            throw std::runtime_error("listAntennas failed: channel must be less than 3.");
    }
    
    return antennas;
}

void SoapyOsmoFL2K::setAntenna(const int direction, const size_t channel, const std::string &name)
{
    if (direction != SOAPY_SDR_TX)
    {
        throw std::runtime_error("setAntena failed: Osmo-FL2K only supports TX");
    }
    
    bool valid = true;
    switch (channel)
    {
        case 0: valid = (name == "red"); break;
        case 1: valid = (name == "green"); break;
        case 2: valid = (name == "blue"); break;
        default:
            throw std::runtime_error("setAntenna failed: channel must be less than 3.");
    }
    
    if (!valid)
    {
        throw std::runtime_error("setAntenna failed: Osmo-FL2K does not support changing the antenna for a channel.");
    }
}

std::string SoapyOsmoFL2K::getAntenna(const int direction, const size_t channel) const
{
	switch (channel)
    {
        case 0: return "red";
        case 1: return "green";
        case 2: return "blue";
        default:
            throw std::runtime_error("getAntenna failed: channel must be less than 3.");
    }
}

/*******************************************************************
 * Frontend corrections API
 ******************************************************************/

bool SoapyOsmoFL2K::hasDCOffsetMode(const int direction, const size_t channel) const
{
    return false;
}

bool SoapyOsmoFL2K::hasFrequencyCorrection(const int direction, const size_t channel) const
{
    return false;
}

/*******************************************************************
 * Sample Rate API
 ******************************************************************/

void SoapyOsmoFL2K::setSampleRate(const int direction, const size_t channel, const double rate)
{
    long long ns = SoapySDR::ticksToTimeNs(ticks, sampleRate);
    resetBuffer = true;
    fl2k_set_sample_rate(dev, rate);
    sampleRate = fl2k_get_sample_rate(dev);
    SoapySDR_logf(SOAPY_SDR_DEBUG, "Setting sample rate: %d", sampleRate);
    ticks = SoapySDR::timeNsToTicks(ns, sampleRate);
}

double SoapyOsmoFL2K::getSampleRate(const int direction, const size_t channel) const
{
    return fl2k_get_sample_rate(dev);
}


void SoapyOsmoFL2K::setBandwidth(const int direction, const size_t channel, const double bw)
{
    SoapySDR::Device::setBandwidth(direction, channel, bw);
}

double SoapyOsmoFL2K::getBandwidth(const int direction, const size_t channel) const
{
    return SoapySDR::Device::getBandwidth(direction, channel);
}

std::vector<double> SoapyOsmoFL2K::listBandwidths(const int direction, const size_t channel) const
{
    std::vector<double> results;

    return results;
}

/*******************************************************************
 * Time API
 ******************************************************************/

std::vector<std::string> SoapyOsmoFL2K::listTimeSources(void) const
{
    std::vector<std::string> results;

    results.push_back("sw_ticks");

    return results;
}

std::string SoapyOsmoFL2K::getTimeSource(void) const
{
    return "sw_ticks";
}

bool SoapyOsmoFL2K::hasHardwareTime(const std::string &what) const
{
    return what == "" || what == "sw_ticks";
}

long long SoapyOsmoFL2K::getHardwareTime(const std::string &what) const
{
    return SoapySDR::ticksToTimeNs(ticks, sampleRate);
}

void SoapyOsmoFL2K::setHardwareTime(const long long timeNs, const std::string &what)
{
    ticks = SoapySDR::timeNsToTicks(timeNs, sampleRate);
}


/*******************************************************************
 * Settings API
 ******************************************************************/

SoapySDR::ArgInfoList SoapyOsmoFL2K::getSettingInfo(void) const
{
    SoapySDR::ArgInfoList setArgs;
    
    SoapySDR::ArgInfo iqSwapArg;

    iqSwapArg.key = "iq_swap";
    iqSwapArg.value = "false";
    iqSwapArg.name = "I/Q Swap";
    iqSwapArg.description = "OSMO-FL2K I/Q Swap Mode";
    iqSwapArg.type = SoapySDR::ArgInfo::BOOL;

    setArgs.push_back(iqSwapArg);

    SoapySDR_logf(SOAPY_SDR_DEBUG, "SETARGS?");

    return setArgs;
}

void SoapyOsmoFL2K::writeSetting(const std::string &key, const std::string &value)
{
}

std::string SoapyOsmoFL2K::readSetting(const std::string &key) const
{
    SoapySDR_logf(SOAPY_SDR_WARNING, "Unknown setting '%s'", key.c_str());
    return "";
}
