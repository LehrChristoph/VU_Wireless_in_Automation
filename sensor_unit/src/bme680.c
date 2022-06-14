#include <zephyr/zephyr.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>

#include "common.h"

#include <logging/log.h>
LOG_MODULE_DECLARE(net_echo_client_sample, LOG_LEVEL_DBG);

void bme680_get_sensor_data(bme680_sensor_data_t *sensor_data)
{
	const struct device *dev = device_get_binding(DT_LABEL(DT_INST(0, bosch_bme680)));
	LOG_DBG("Device %p name is %s\n", dev, dev->name);
	int ret = device_is_ready(dev);
	if(!ret )
	{
		LOG_ERR("Device %s is not ready \n",
		       dev->name);
		return;
	}
	
	ret = sensor_sample_fetch(dev);
	if(ret != 0)
	{
		LOG_ERR("Unable to fetch sensor sample of %s: %i \n",
		       dev->name, -ret);
		return;
	}
	
    ret = sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &sensor_data->temp);
	if(ret != 0)
	{
		LOG_ERR("Unable to sensor data %i of %s: %i \n",
		       SENSOR_CHAN_AMBIENT_TEMP, dev->name, -ret);
		return;
	}
	
	ret = sensor_channel_get(dev, SENSOR_CHAN_PRESS, &sensor_data->press);
	if(ret != 0)
	{
		LOG_ERR("Unable to sensor data %i of %s: %i \n",
		       SENSOR_CHAN_PRESS, dev->name, -ret);
		return;
	}
	ret = sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &sensor_data->humidity);
	if(ret != 0)
	{
		LOG_ERR("Unable to sensor data %i of %s: %i \n",
		       SENSOR_CHAN_HUMIDITY, dev->name, -ret);
		return;
	}
	ret = sensor_channel_get(dev, SENSOR_CHAN_GAS_RES, &sensor_data->gas_res);
	if(ret != 0)
	{
		LOG_ERR("Unable to sensor data for channle %i of %s: %i \n",
		       SENSOR_CHAN_GAS_RES, dev->name, -ret);
		return;
	}

    LOG_DBG("T: %d.%06d; P: %d.%06d; H: %d.%06d; G: %d.%06d\n",
				sensor_data->temp.val1, sensor_data->temp.val2, 
				sensor_data->press.val1, sensor_data->press.val2,
				sensor_data->humidity.val1, sensor_data->humidity.val2, 
				sensor_data->gas_res.val1, sensor_data->gas_res.val2);
	
}