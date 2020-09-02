#include <cstdio>	//stdandard output
#include <cstdlib>

#include <SoapySDR/Device.hpp>
#include <SoapySDR/Types.hpp>
#include <SoapySDR/Formats.hpp>
#include "SoapyOsmoFL2K.hpp"

#include <string>	// std::string
#include <vector>	// std::vector<...>
#include <map>		// std::map< ... , ... >
#include <cmath>

#include <iostream>

static void MakeCosineTable(float buff[], size_t numElements)
{
    const float radiansPerSample = 2 * M_PI / numElements;
    for (int i = 0; i < numElements; i++)
    {
        buff[i] = cos(i * radiansPerSample);
    }
}

static int TestTx(SoapySDR::Device *sdr, std::vector<std::string> &str_list)
{
    str_list = sdr->listGains( SOAPY_SDR_TX, 0);
    printf("Tx Gains: ");
    for(int i = 0; i < str_list.size(); ++i)
        printf("%s, ", str_list[i].c_str());
    printf("\n");
    
    //    2.3. ranges(frequency ranges)
    SoapySDR::RangeList ranges = sdr->getFrequencyRange( SOAPY_SDR_TX, 0);
    printf("Tx freq ranges: ");
    for(int i = 0; i < ranges.size(); ++i)
        printf("[%g Hz -> %g Hz], ", ranges[i].minimum(), ranges[i].maximum());
    printf("\n");
    
    // 3. apply settings
    sdr->setSampleRate( SOAPY_SDR_TX, 0, 10e6);
    
    sdr->setFrequency( SOAPY_SDR_TX, 0, 433e6);
    
    // 4. setup a stream (real floats)
    SoapySDR::Stream *tx_stream = sdr->setupStream( SOAPY_SDR_TX, SOAPY_SDR_F32);
    if( tx_stream == NULL)
    {
        std::cerr << "Failed\n";
        SoapySDR::Device::unmake( sdr );
        return EXIT_FAILURE;
    }
    sdr->activateStream( tx_stream, 0, 0, 0);
    
    // 5. create a re-usable buffer for TX samples
    float buff[102400];
    
    MakeCosineTable(buff, sizeof(buff) / sizeof(buff[0]));
    
    // 6. Transmit some samples
    int ret;
    for( int i = 0; i < 200; ++i)
    {
        void *buffs[] = {buff};
        int flags = 0;
        long long time_ns;
        ret = sdr->writeStream( tx_stream, buffs, sizeof(buff) / sizeof(buff[0]), flags, time_ns, 1e5);
        
        printf("ret = ");
        if (ret < 0)
        {
            printf("%s", SoapySDR_errToStr(ret));
        }
        else
        {
            printf("%d", ret);
        }
        printf(", flags = %d, time_ns = %lld\n", flags, time_ns);
    }
    
    // Get the stream status
    size_t chanMask;
    int flags;
    long long timeNs;
    ret = sdr->readStreamStatus(tx_stream, chanMask, flags, timeNs, 1e6);
    printf("readStreamStatus(): %s\n", SoapySDR_errToStr(ret));
    
    // 7. shutdown the stream
    ret = sdr->deactivateStream( tx_stream, 0, 0);    //stop streaming
    ret = sdr->readStreamStatus(tx_stream, chanMask, flags, timeNs, 2e6);
    printf("readStreamStatus(): %s\n", SoapySDR_errToStr(ret));
    sdr->closeStream( tx_stream );
}

static void TestChannel(SoapySDR::Device *sdr, int channelNumber)
{
    std::vector< std::string > str_list;    //string list
    
    //    2.1 antennas
    str_list = sdr->listAntennas( SOAPY_SDR_RX, channelNumber);
    printf("Rx antennas: ");
    for(int i = 0; i < str_list.size(); ++i)
        printf("%s,", str_list[i].c_str());
    printf("\n");
    
    str_list = sdr->listAntennas( SOAPY_SDR_TX, channelNumber);
    printf("Tx antennas: ");
    for(int i = 0; i < str_list.size(); ++i)
        printf("%s,", str_list[i].c_str());
    printf("\n");
    
    if (str_list.size() > 0)
    {
        TestTx(sdr, str_list);
        //SoapySDR::Stream *stream = sdr->setupStream(SOAPY_SDR_TX, SOAPY_SDR_CS8);
        //sdr->closeStream(stream);
    }
}

int main()
{

	// 0. enumerate devices (list all devices' information)
	SoapySDR::KwargsList results = SoapySDR::Device::enumerate();
	SoapySDR::Kwargs::iterator it;

	for( int i = 0; i < results.size(); ++i)
	{
		printf("Found device #%d: ", i);
		for( it = results[i].begin(); it != results[i].end(); ++it)
		{
			printf("%s = %s\n", it->first.c_str(), it->second.c_str());
		}
		printf("\n");
	}

	// 1. create device instance
	
	//	1.1 set arguments
	//		args can be user defined or from the enumeration result
	//		We use first results as args here:
	SoapySDR::Kwargs args = results[0];

	//	1.2 make device
	SoapySDR::Device *sdr = SoapySDR::Device::make(args);

	if( sdr == NULL )
	{
		fprintf(stderr, "SoapySDR::Device::make failed\n");
		return EXIT_FAILURE;
	}

	// 2. query device info
    
    // Get the number of TX channels, then loop our test through each.
    const int numChannels = sdr->getNumChannels(SOAPY_SDR_TX);
    
    for (int i = 0; i < numChannels; i++)
    {
        TestChannel(sdr, i);
    }

	// 8. cleanup device handle
	SoapySDR::Device::unmake( sdr );
	printf("Done\n");

	return EXIT_SUCCESS;
}
