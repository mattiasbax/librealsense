// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2021 Intel Corporation. All Rights Reserved.

#include <iostream>
#include <chrono>
#include "ds5-color.h"
#include "ds5-private.h"
#include "ds5-thermal-monitor.h"

namespace librealsense
{
    ds5_thermal_monitor::ds5_thermal_monitor(synthetic_sensor& activation_sensor,
                                            std::shared_ptr<option> temp_option,
                                            std::shared_ptr<option> tl_toggle) :
        _monitor([this](dispatcher::cancellable_timer cancellable_timer)
            {
                polling(cancellable_timer);
            }),
        _poll_intervals_ms(2000), // Temperature check routine to be invoked every 2 sec
        _thermal_threshold_deg(2.f),
        _temp_base(0.f),
        _temperature_sensor(temp_option),
        _tl_activation(tl_toggle)
     {
        _dpt_sensor = std::dynamic_pointer_cast<synthetic_sensor>(activation_sensor.shared_from_this());
    }

    ds5_thermal_monitor::~ds5_thermal_monitor()
    {
        stop();
    }

    void ds5_thermal_monitor::start()
    {
        if (!_monitor.is_active())
            _monitor.start();
    }

    void ds5_thermal_monitor::stop()
    {
        if (_monitor.is_active())
        {
            _monitor.stop();
            _temp_base = 0.f;
        }
    }

    void ds5_thermal_monitor::update(bool on)
    {
        if (on != _monitor.is_active())
        {
            if (auto snr = _dpt_sensor.lock())
            {
                if (!on)
                {
                    stop();
                    notify(0);
                }
                else
                    if (snr->is_opened())
                    {
                        start();
                    }
            }
        }
    }

    void ds5_thermal_monitor::polling(dispatcher::cancellable_timer cancellable_timer)
    {
        if (cancellable_timer.try_sleep(_poll_intervals_ms))
        {
            try
            {
                // Verify TL is active on FW level
                if (auto tl_active = _tl_activation.lock())
                {
                    if (std::fabs(tl_active->query()) < std::numeric_limits< float >::epsilon())
                        return;
                }

                // Track temperature and update on temperature changes
                auto ts = (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
                if (auto temp = _temperature_sensor.lock())
                {
                    auto cur_temp = temp->query();

                    if (fabs(_temp_base - cur_temp) >= _thermal_threshold_deg)
                    {
                        LOG_DEBUG_THERMAL_LOOP("Thermal calibration adjustment is triggered on change from "
                            << std::dec << std::setprecision(1) << _temp_base << " to " << cur_temp << " deg (C)");

                        notify(cur_temp);
                        _temp_base = cur_temp;
                    }
                }
                else
                {
                    LOG_ERROR("Thermal Compensation: temperature sensor option is not present");
                }
            }
            catch (const std::exception& ex)
            {
                LOG_ERROR("Error during thermal compensation handling: " << ex.what());
            }
            catch (...)
            {
                LOG_ERROR("Unresolved error during Thermal Compensation handling");
            }
        }
        else
        {
            LOG_DEBUG_THERMAL_LOOP("Thermal Compensation is being shut-down");
        }
    }

    void ds5_thermal_monitor::notify(float temperature) const
    {
        for (auto&& cb : _thermal_changes_callbacks)
            cb(temperature);
    }
}
