/*
 * The MIT License (MIT)
 * 
 * Copyright (c) 2015 Charles J. Cliffe
 * Copyright (c) 2015-2017 Josh Blum

 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "SoapyOsmoFL2K.hpp"
#include <SoapySDR/Logger.hpp>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Time.hpp>
#include <osmo-fl2k.h>
#include <algorithm> //min
#include <climits> //SHRT_MAX
#include <cstring> // memcpy
#include <iostream>           // std::cout
#include <iomanip>


std::vector<std::string> SoapyOsmoFL2K::getStreamFormats(const int direction, const size_t channel) const
{
    std::vector<std::string> formats;

    formats.push_back(SOAPY_SDR_S8);
    formats.push_back(SOAPY_SDR_S16);
    formats.push_back(SOAPY_SDR_U8);
    formats.push_back(SOAPY_SDR_U16);
    formats.push_back(SOAPY_SDR_F32);

    return formats;
}

std::string SoapyOsmoFL2K::getNativeStreamFormat(const int direction, const size_t channel, double &fullScale) const
{
    //check that direction is SOAPY_SDR_TX
     if (direction != SOAPY_SDR_TX)
     {
         throw std::runtime_error("Osmo-FL2K is TX only, use SOAPY_SDR_TX");
     }

     fullScale = 255;
     return SOAPY_SDR_U8;
}

SoapySDR::ArgInfoList SoapyOsmoFL2K::getStreamArgsInfo(const int direction, const size_t channel) const
{
    //check that direction is SOAPY_SDR_TX
     if (direction != SOAPY_SDR_TX)
     {
         throw std::runtime_error("Osmo-FL2K is TX only, use SOAPY_SDR_TX");
     }

    SoapySDR::ArgInfoList streamArgs;

    SoapySDR::ArgInfo bufflenArg;
    bufflenArg.key = "bufflen";
    bufflenArg.value = std::to_string(DEFAULT_BUFFER_LENGTH);
    bufflenArg.name = "Buffer Size";
    bufflenArg.description = "Number of bytes per buffer, multiples of 512 only.";
    bufflenArg.units = "bytes";
    bufflenArg.type = SoapySDR::ArgInfo::INT;

    streamArgs.push_back(bufflenArg);

    SoapySDR::ArgInfo buffersArg;
    buffersArg.key = "buffers";
    buffersArg.value = std::to_string(DEFAULT_NUM_BUFFERS);
    buffersArg.name = "Ring buffers";
    buffersArg.description = "Number of buffers in the ring.";
    buffersArg.units = "buffers";
    buffersArg.type = SoapySDR::ArgInfo::INT;

    streamArgs.push_back(buffersArg);

    SoapySDR::ArgInfo asyncbuffsArg;
    asyncbuffsArg.key = "asyncBuffs";
    asyncbuffsArg.value = "0";
    asyncbuffsArg.name = "Async buffers";
    asyncbuffsArg.description = "Number of async usb buffers (advanced).";
    asyncbuffsArg.units = "buffers";
    asyncbuffsArg.type = SoapySDR::ArgInfo::INT;

    streamArgs.push_back(asyncbuffsArg);

    return streamArgs;
}

/*******************************************************************
 * Async thread work
 ******************************************************************/

static void _tx_callback(fl2k_data_info_t *data_info)
{
    //printf("_tx_callback\n");
    SoapyOsmoFL2K *self = (SoapyOsmoFL2K *) (data_info->ctx);
    self->tx_callback(data_info);
}

const std::string &SoapyOsmoFL2K::fl2kErrorToString(enum fl2k_error error)
{
    static const std::map<enum fl2k_error, std::string> errorToStringMap =
    {
        { FL2K_SUCCESS,             "FL2K_SUCCESS" },
        { FL2K_TRUE,                "FL2K_TRUE" },
        { FL2K_ERROR_INVALID_PARAM, "FL2K_ERROR_INVALID_PARAM" },
        { FL2K_ERROR_NO_DEVICE,     "FL2K_ERROR_NO_DEVICE" },
        { FL2K_ERROR_NOT_FOUND,     "FL2K_ERROR_NOT_FOUND" },
        { FL2K_ERROR_BUSY,          "FL2K_ERROR_BUSY" },
        { FL2K_ERROR_TIMEOUT,       "FL2K_ERROR_TIMEOUT" },
        { FL2K_ERROR_NO_MEM,        "FL2K_ERROR_NO_MEM" },
    };
    
    // TODO: Is this thread safe?
    std::map<enum fl2k_error, std::string>::const_iterator it = errorToStringMap.find(error);
    if (it == errorToStringMap.end())
    {
        throw std::runtime_error("Invalid fl2k_error: " + std::to_string(error));
    }
    return it->second;
}

const std::string &SoapyOsmoFL2K::fl2kErrorToString(int error)
{
    return fl2kErrorToString((enum fl2k_error) error);
}

void SoapyOsmoFL2K::tx_callback(fl2k_data_info_t *data_info)
{
    std::cout
        << std::setfill('0') << std::setw(8)
        << std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::system_clock::now() - start).count()
        << ": tx_callback: _buf_count="
        << (int) _buf_count
        << ", device_error="
        << data_info->device_error
        << ", underflow_cnt="
        << data_info->underflow_cnt
        << "\n";

    // atomically add len to ticks but return the previous value
    unsigned long long tick = ticks.fetch_add(data_info->len);

    // Check the underflow count and exit early if there was an underflow
    if (data_info->underflow_cnt > 0)
    {
        std::cout
            << std::setfill('0') << std::setw(8)
            << std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::system_clock::now() - start).count()
            << ": tx_callback: underflow_cnt > 0\n";
        _underflowEvent = true;
        return;
    }
    
    if (_buf_count >= _buffs.size())
    {
        std::cout
            << std::setfill('0') << std::setw(8)
            << std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::system_clock::now() - start).count()
            << ": tx_callback: underflow: _buf_count >= _buffs.size()\n";
        _underflowEvent = true;
        return;
    }

    // Give the driver the next filled buffer
    auto &buff = _buffs[_buf_tail];
    buff.tick = tick;
    data_info->r_buf = (char *) buff.red;
    data_info->g_buf = (char *) buff.green;
    data_info->b_buf = (char *) buff.blue;
    data_info->sampletype_signed = _signed;
    
    // Advance the tail to point to the next buffer
    _buf_tail = (_buf_tail + 1) % _buffs.size();
    
    // Increment buffers available under lock to avoid race in acquireWriteBuffer wait
    {
     std::lock_guard<std::mutex> lock(_buf_mutex);
     _buf_count++;
    }

    // Notify acquireBuffer()
    _buf_cond.notify_one();
}

/*******************************************************************
 * Stream API
 ******************************************************************/

SoapySDR::Stream *SoapyOsmoFL2K::setupStream(
        const int direction,
        const std::string &format,
        const std::vector<size_t> &channels,
        const SoapySDR::Kwargs &args)
{
    start = std::chrono::system_clock::now();
    
    if (direction != SOAPY_SDR_TX)
    {
        throw std::runtime_error("Osmo-FL2K is TX only, use SOAPY_SDR_TX");
    }

    //check the channel configuration
    if (channels.size() > 1 or (channels.size() > 0 and channels.at(0) != 0))
    {
        throw std::runtime_error("setupStream invalid channel selection");
    }

    //check the format
    _signed = false;
    if (format == SOAPY_SDR_F32)
    {
        SoapySDR_log(SOAPY_SDR_INFO, "Using format F32.");
        txFormat = FL2K_TX_FORMAT_FLOAT32;
        _signed = true;
    }
    else if (format == SOAPY_SDR_S16)
    {
        SoapySDR_log(SOAPY_SDR_INFO, "Using format S16.");
        txFormat = FL2K_TX_FORMAT_INT16;
        _signed = true;
    }
    else if (format == SOAPY_SDR_S8) {
        SoapySDR_log(SOAPY_SDR_INFO, "Using format S8.");
        txFormat = FL2K_TX_FORMAT_INT8;
        _signed = true;
    }
    else if (format == SOAPY_SDR_U16)
    {
        SoapySDR_log(SOAPY_SDR_INFO, "Using format U16.");
        txFormat = FL2K_TX_FORMAT_UINT16;
    }
    else if (format == SOAPY_SDR_U8) {
        SoapySDR_log(SOAPY_SDR_INFO, "Using format U8.");
        txFormat = FL2K_TX_FORMAT_UINT8;
    }
    else
    {
        throw std::runtime_error(
                "setupStream invalid format '" + format
                        + "' -- Only S8, S16, U8, U16 and F32 are supported by the SoapyOsmoFL2K module.");
    }

    // TODO: FL2K doesn't support a variable buffer length. It's fixed. Do we need to support this argument?
    bufferLength = DEFAULT_BUFFER_LENGTH;
    if (args.count("bufflen") != 0)
    {
        try
        {
            int bufferLength_in = std::stoi(args.at("bufflen"));
            if (bufferLength_in > 0)
            {
                bufferLength = bufferLength_in;
            }
        }
        catch (const std::invalid_argument &){}
    }
    SoapySDR_logf(SOAPY_SDR_DEBUG, "Osmo-FL2K Using buffer length %d", bufferLength);

    asyncBuffs = DEFAULT_NUM_BUFFERS;
    if (args.count("buffers") != 0)
    {
        try
        {
            int numBuffers_in = std::stoi(args.at("buffers"));
            if (numBuffers_in > 0)
            {
                asyncBuffs = numBuffers_in;
            }
        }
        catch (const std::invalid_argument &){}
    }

    if (args.count("asyncBuffs") != 0)
    {
        try
        {
            int asyncBuffs_in = std::stoi(args.at("asyncBuffs"));
            if (asyncBuffs_in > 0)
            {
                asyncBuffs = asyncBuffs_in;
            }
        }
        catch (const std::invalid_argument &){}
    }
    SoapySDR_logf(SOAPY_SDR_DEBUG, "Osmo-FL2K Using %d buffers", asyncBuffs);
    
    // Clear async fifo counts
    _buf_tail = 0;
    _buf_count = asyncBuffs;
    _buf_head = 0;

    // Allocate buffers
    _buffs.resize(asyncBuffs);

    return (SoapySDR::Stream *) this;
}

void SoapyOsmoFL2K::closeStream(SoapySDR::Stream *stream)
{
    this->deactivateStream(stream, 0, 0);
    _buffs.clear();
}

size_t SoapyOsmoFL2K::getStreamMTU(SoapySDR::Stream *stream) const
{
    return bufferLength / BYTES_PER_SAMPLE;
}

int SoapyOsmoFL2K::activateStream(
        SoapySDR::Stream *stream,
        const int flags,
        const long long timeNs,
        const size_t numElems)
{
    if (flags != 0) return SOAPY_SDR_NOT_SUPPORTED;
    resetBuffer = true;
    bufferedElems = 0;
    
    return fl2k_start_tx(dev, &_tx_callback, this, asyncBuffs);
}

int SoapyOsmoFL2K::deactivateStream(SoapySDR::Stream *stream, const int flags, const long long timeNs)
{
    if (flags != 0) return SOAPY_SDR_NOT_SUPPORTED;
    
    // Release all the buffers first.
    int numDirectAccessBuffers = getNumDirectAccessBuffers(stream);
    for (int i = 0; i < numDirectAccessBuffers; i++)
    {
        int presentFlags = flags;
        releaseWriteBuffer(stream, _currentHandle, 0, presentFlags, timeNs);
    }
    
    fl2k_error ret = (fl2k_error) fl2k_stop_tx(dev);
    
    if (ret != FL2K_SUCCESS)
    {
        std::cerr << "WARNING: SoapyOsmoFL2K::deactivateStream(): " << SoapyOsmoFL2K::fl2kErrorToString(ret) << "\n";
    }
    
    return ret;
}

int SoapyOsmoFL2K::writeStreamForChannel(
        const void * const buff,
        unsigned char *currentBuff,
        size_t returnedElems)
{
     // Translate from signed float to unsigned uchar
     if (txFormat == FL2K_TX_FORMAT_FLOAT32)
     {
         float *fsource = (float *) buff;
         _signed = true;
         for (size_t i = 0; i < returnedElems; i++)
         {
             float scaled = fsource[i] * 128.0;
             int8_t truncated = (int8_t) scaled;
             currentBuff[i] = truncated;
             //_currentBuff[i] = (int8_t) (fsource[i] * 255.0);
         }
     }
     else if (txFormat == FL2K_TX_FORMAT_INT16)
     {
         int16_t *isource = (int16_t *) buff;
         _signed = true;
         for (size_t i = 0; i < returnedElems; i++)
         {
             // TODO: Check this for correctness.
             currentBuff[i] = isource[i] / 8;
         }
     }
     else if (txFormat == FL2K_TX_FORMAT_INT8)
     {
         _signed = true;
     }
     else if (txFormat == FL2K_TX_FORMAT_UINT16)
     {
         int16_t *isource = (int16_t *) buff;
         _signed = false;
         for (size_t i = 0; i < returnedElems; i++)
         {
             // TODO: Check this for correctness.
             currentBuff[i] = isource[i] / 8;
         }
     }
     else if (txFormat == FL2K_TX_FORMAT_UINT8)
     {
         _signed = false;
     }
}

int SoapyOsmoFL2K::writeStream(
        SoapySDR::Stream *stream,
        const void * const *buffs,
        const size_t numElems,
        int &flags,
        const long long timeNs,
        const long timeoutUs)
{
    // Drop the remainder of the buffer on reset
    if (resetBuffer and bufferedElems != 0)
    {
        bufferedElems = 0;
        this->releaseWriteBuffer(stream, _currentHandle, numElems, flags, timeNs);
    }
    
    //are elements left in the buffer? if not, do a new write.
    if (bufferedElems == 0)
    {
        int ret = this->acquireWriteBuffer(stream, _currentHandle, (void **) _currentBuffs, timeoutUs);
        if (ret < 0) return ret;
        bufferedElems = ret;
    }

    //otherwise just update return time to the current tick count
    // TODO: The time isn't for updating, it's for timing out!
    //else
    //{
    //    flags |= SOAPY_SDR_HAS_TIME;
    //    //timeNs = SoapySDR::ticksToTimeNs(bufTicks, sampleRate);
    //}

    size_t returnedElems = std::min(bufferedElems, numElems);

    for (int i = 0; i < 3; i++)
    {
        this->writeStreamForChannel(buffs[i], _currentBuffs[i], returnedElems);
        
        //bump variables for next call into writeStream
        _currentBuffs[i] += returnedElems*BYTES_PER_SAMPLE;
    }
                      
    //bump variables for next call into writeStream
    bufferedElems -= returnedElems;
    bufTicks += returnedElems; //for the next call to writeStream if there is a remainder
                                    
    //return number of elements written to buff
    if (bufferedElems != 0) flags |= SOAPY_SDR_MORE_FRAGMENTS;
    else this->releaseWriteBuffer(stream, _currentHandle, numElems, flags, timeNs);
    return returnedElems;
}

int SoapyOsmoFL2K::readStreamStatus(
        SoapySDR::Stream *stream,
        size_t &chanMask,
        int &flags,
        long long &timeNs,
        const long timeoutUs)
{

    // TODO: Check *stream

    //calculate when the loop should exit
    const auto timeout = std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(std::chrono::microseconds(timeoutUs));
    const auto exitTime = std::chrono::high_resolution_clock::now() + timeout;

    //poll for status events until the timeout expires
    while (true)
    {
         if(_underflowEvent)
         {
             _underflowEvent = false;
             SoapySDR::log(SOAPY_SDR_SSI, "U");
             return SOAPY_SDR_UNDERFLOW;
         }
        

        // sleep for a fraction of the total timeout
        const auto sleepTimeUs = std::min<long>(1000, timeoutUs/10);
        std::this_thread::sleep_for(std::chrono::microseconds(sleepTimeUs));

        //check for timeout expired
        const auto timeNow = std::chrono::high_resolution_clock::now();
        if (exitTime < timeNow) return SOAPY_SDR_TIMEOUT;
    }
}


/*******************************************************************
 * Direct buffer access API
 ******************************************************************/

size_t SoapyOsmoFL2K::getNumDirectAccessBuffers(SoapySDR::Stream *stream)
{
    return _buffs.size();;
}

int SoapyOsmoFL2K::getDirectAccessBufferAddrs(SoapySDR::Stream *stream, const size_t handle, void **buffs)
{
    buffs[0] = (void *) _buffs[handle].red;
    buffs[1] = (void *) _buffs[handle].green;
    buffs[2] = (void *) _buffs[handle].blue;
    return 0;
}

int SoapyOsmoFL2K::acquireWriteBuffer(
    SoapySDR::Stream *stream,
    size_t &handle,
    void **buffs,
    const long timeoutUs)
{
    std::cout
        << std::setfill('0') << std::setw(8)
        << std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::system_clock::now() - start).count()
        << ": acquireWriteBuffer: _buf_count="
        << (int) _buf_count
        << "\n";
    
    // Reset is issued by various settings to drain old data out of the queue
    if (resetBuffer)
    {
        // Drain all buffers from the fifo
        _buf_head = (_buf_head + _buf_count.exchange(_buffs.size())) % _buffs.size();
        resetBuffer = false;
        _underflowEvent = false;
    }

    // Handle overflow from the tx callback thread
    if (_underflowEvent)
    {
        // Drain the old buffers from the fifo
        _buf_head = (_buf_head + _buf_count.exchange(_buffs.size())) % _buffs.size();
        _underflowEvent = false;
        SoapySDR::log(SOAPY_SDR_SSI, "U");
        return SOAPY_SDR_UNDERFLOW;
    }

    // Wait a buffer to become available
    std::unique_lock <std::mutex> lock(_buf_mutex);
    if (_buf_cond.wait_for(lock, std::chrono::microseconds(timeoutUs), [this]{return _buf_count != 0;}) == false)
    {
        return SOAPY_SDR_TIMEOUT;
    }
    std::cout
        << std::setfill('0') << std::setw(8)
        << std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::system_clock::now() - start).count()
        << ": acquireWriteBuffer: finished waiting to acquire buffer\n";
    
    // Extract handle and buffer
    handle = _buf_head;
    _buf_head = (_buf_head + 1) % _buffs.size();
    bufTicks = _buffs[handle].tick;
    //timeNs = SoapySDR::ticksToTimeNs(_buffs[handle].tick, sampleRate);
    
    buffs[0] = (void *)_buffs[handle].red;
    buffs[1] = (void *)_buffs[handle].green;
    buffs[2] = (void *)_buffs[handle].blue;
    //flags = SOAPY_SDR_HAS_TIME;

    // Return the number of elements available
    return sizeof(_buffs[handle].red) / BYTES_PER_SAMPLE;
}


void SoapyOsmoFL2K::releaseWriteBuffer(
    SoapySDR::Stream *stream,
    const size_t handle,
    const size_t numElems,
    int &flags,
    const long long timeNs)
{
    // Increment the number of filled buffers
    _buf_count--;
}
