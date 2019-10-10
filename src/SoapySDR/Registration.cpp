#include "SoapyOsmoFL2K.hpp"
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Registry.hpp>
#include <osmo-fl2k.h>

int SoapyOsmoFL2K::fl2k_count;
std::vector<SoapySDR::Kwargs> SoapyOsmoFL2K::fl2k_devices;


/***********************************************************************
 * Find available devices
 **********************************************************************/
SoapySDR::KwargsList findOsmoFL2K(const SoapySDR::Kwargs &args)
{
	std::vector<SoapySDR::Kwargs> results;
    int this_count = fl2k_get_device_count();
    
    // Check if the device list needs to be refreshed. It does if we don't have a 
    // record of any devices or our record has a different number of devices than we
    // just got from the driver.
    if (!SoapyOsmoFL2K::fl2k_devices.size() || SoapyOsmoFL2K::fl2k_count != this_count)
    {
    	SoapyOsmoFL2K::fl2k_count = this_count;
    	
    	// Erase our record of the devices if the list isn't already empty.
    	if (SoapyOsmoFL2K::fl2k_devices.size())
        {
            SoapyOsmoFL2K::fl2k_devices.erase(SoapyOsmoFL2K::fl2k_devices.begin(), SoapyOsmoFL2K::fl2k_devices.end());
        }
        SoapySDR_logf(SOAPY_SDR_DEBUG, "Osmo-FL2K Devices: %d", SoapyOsmoFL2K::fl2k_count);
        
        // Now fill in the device list.
        for (int i = 0; i < SoapyOsmoFL2K::fl2k_count; i++)
        {
            SoapySDR::Kwargs devInfo;

            std::string deviceName(fl2k_get_device_name(i));
            
            bool deviceAvailable = false;
            SoapySDR_logf(SOAPY_SDR_DEBUG, "Device #%d: %s", i, deviceName.c_str());
            
            // Open the device to make sure it's available
			fl2k_dev_t *devTest;
			if (fl2k_open(&devTest, i) == 0)
			{
				deviceAvailable = true;
				fl2k_close(devTest);
			}
			
			if (!deviceAvailable)
            {
                SoapySDR_logf(SOAPY_SDR_DEBUG, "\tUnable to access device #%d (in use?)", i);
            }

			// TODO: Find a serial number somehow
            std::string deviceLabel = deviceName + " :: "; // + deviceSerial;

            devInfo["fl2k"] = std::to_string(i);
            devInfo["label"] = deviceLabel;
            devInfo["available"] = deviceAvailable ? "Yes" : "No";
            SoapyOsmoFL2K::fl2k_devices.push_back(devInfo);
        }
    }
    
    // Do the filtering based on the passed-in args
    for (int i = 0; i < SoapyOsmoFL2K::fl2k_count; i++)
    {
        SoapySDR::Kwargs devInfo = SoapyOsmoFL2K::fl2k_devices[i];
        if (args.count("fl2k") != 0)
        {
            if (args.at("fl2k") != devInfo.at("fl2k"))
            {
                continue;
            }
            SoapySDR_logf(SOAPY_SDR_DEBUG, "Found device by index %s", devInfo.at("fl2k").c_str());
        }
        else if (args.count("label") != 0)
        {
            if (devInfo.at("label") != args.at("label"))
            {
                continue;
            }
            SoapySDR_logf(SOAPY_SDR_DEBUG, "Found device by label %s", args.at("label").c_str());
        }
        results.push_back(SoapyOsmoFL2K::fl2k_devices[i]);
    }
    return results;
}

/***********************************************************************
 * Make device instance
 **********************************************************************/
SoapySDR::Device *makeOsmoFL2K(const SoapySDR::Kwargs &args)
{
    //create an instance of the device object given the args
    //here we will translate args into something used in the constructor
    return new SoapyOsmoFL2K(args);
}

/***********************************************************************
 * Registration
 **********************************************************************/
static SoapySDR::Registry registerOsmoFL2K("osmo_fl2k", &findOsmoFL2K, &makeOsmoFL2K, SOAPY_SDR_ABI_VERSION);
