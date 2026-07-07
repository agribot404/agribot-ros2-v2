import React, { useState, useEffect, useRef } from 'react';
import {
  BiDroplet,
  BiSun,
  BiCloudRain,
  BiTachometer,
  BiCameraMovie,
  BiCheckCircle,
  BiXCircle,
  BiEdit,
  BiWifiOff,
} from 'react-icons/bi';
import { WiHumidity } from 'react-icons/wi';
import { subscribeTopic } from '../lib/ros';
import { fetchLatestLog } from '../lib/api';

// ---------------------------------------------------------------------------
//  Interfaces
// ---------------------------------------------------------------------------

interface WeatherData {
  condition: string;
  temp_c: number;
  humidity: number;
  chance_of_rain: number;
  will_it_rain: boolean;
  uv_index: number;
  wind_kph: number;
  aqi_us_epa: number;
  aqi_co: number;
  aqi_no2: number;
  aqi_o3: number;
  aqi_so2: number;
  aqi_pm25: number;
  aqi_pm10: number;
  last_updated: number;
}

// ---------------------------------------------------------------------------
//  Component
// ---------------------------------------------------------------------------

export const Dashboard: React.FC = () => {
  // Sensor state (live from ROS)
  const [temperature, setTemperature] = useState<number | null>(null);
  const [humidity, setHumidity] = useState<number | null>(null);
  const [lastSensorUpdate, setLastSensorUpdate] = useState<number | null>(null);

  // Soil moisture (last known from REST API)
  const [soilPercent, setSoilPercent] = useState<number | null>(null);
  const [soilUpdated, setSoilUpdated] = useState<string | null>(null);

  // Weather
  const [weather, setWeather] = useState<WeatherData | null>(null);

  // Camera
  const [camURL, setCamURL] = useState<string>(
    localStorage.getItem('agribot_cam_url') || 'http://agricam.local:81/stream',
  );
  const [isEditingCam, setIsEditingCam] = useState<boolean>(false);
  const camInputRef = useRef<HTMLInputElement>(null);

  // ------------------------------------------------------------------
  //  ROS subscriptions (live sensor data)
  // ------------------------------------------------------------------

  useEffect(() => {
    const tempTopic = subscribeTopic<{ temperature: number }>(
      'sensor/dht11_temperature',
      'sensor_msgs/msg/Temperature',
      (msg) => {
        setTemperature(msg.temperature);
        setLastSensorUpdate(Date.now());
      },
    );

    const humTopic = subscribeTopic<{ relative_humidity: number }>(
      'sensor/dht11_humidity',
      'sensor_msgs/msg/RelativeHumidity',
      (msg) => {
        // micro-ROS publishes 0.0-1.0, display as 0-100 %
        setHumidity(msg.relative_humidity * 100);
        setLastSensorUpdate(Date.now());
      },
    );

    return () => {
      tempTopic.unsubscribe();
      humTopic.unsubscribe();
    };
  }, []);

  // ------------------------------------------------------------------
  //  Fetch last soil moisture reading from REST API
  // ------------------------------------------------------------------

  useEffect(() => {
    fetchLatestLog('manual').then((entry) => {
      if (entry && entry.soil_percent !== null) {
        setSoilPercent(entry.soil_percent);
        setSoilUpdated(entry.timestamp);
      }
    });
  }, []);

  // ------------------------------------------------------------------
  //  Weather API polling (every 10 minutes)
  // ------------------------------------------------------------------

  useEffect(() => {
    const fetchWeather = async () => {
      try {
        const apiKey = import.meta.env.VITE_WEATHER_API_KEY;
        if (!apiKey || apiKey === 'YOUR_WEATHER_API_KEY') return;

        const res = await fetch(
          `https://api.weatherapi.com/v1/forecast.json?key=${apiKey}&q=Rajshahi&days=1&aqi=yes&alerts=no`,
        );
        const data = await res.json();

        if (data?.current) {
          const aqi = data.current.air_quality || {};
          setWeather({
            condition: data.current.condition?.text || '',
            temp_c: data.current.temp_c,
            humidity: data.current.humidity,
            chance_of_rain:
              data.forecast?.forecastday?.[0]?.day?.daily_chance_of_rain || 0,
            will_it_rain:
              data.forecast?.forecastday?.[0]?.day?.daily_will_it_rain === 1,
            uv_index: data.current.uv,
            wind_kph: data.current.wind_kph,
            aqi_us_epa: aqi['us-epa-index'] || 0,
            aqi_co: aqi.co || 0,
            aqi_no2: aqi.no2 || 0,
            aqi_o3: aqi.o3 || 0,
            aqi_so2: aqi.so2 || 0,
            aqi_pm25: aqi.pm2_5 || 0,
            aqi_pm10: aqi.pm10 || 0,
            last_updated: Date.now(),
          });
        }
      } catch (err) {
        console.error('Failed to fetch weather data:', err);
      }
    };

    fetchWeather();
    const interval = setInterval(fetchWeather, 600_000);
    return () => clearInterval(interval);
  }, []);

  // ------------------------------------------------------------------
  //  Camera helpers
  // ------------------------------------------------------------------

  const saveCamURL = (url: string) => {
    setCamURL(url);
    localStorage.setItem('agribot_cam_url', url);
  };

  // ------------------------------------------------------------------
  //  Derived state
  // ------------------------------------------------------------------

  const needsIrrigation =
    soilPercent !== null && soilPercent < 30 && weather && !weather.will_it_rain;

  const aqiLabel = (val: number) => {
    if (val === 0) return '--';
    if (val === 1) return '1 - Good';
    if (val === 2) return '2 - Moderate';
    if (val === 3) return '3 - Unhealthy for Sensitive Groups';
    if (val === 4) return '4 - Unhealthy';
    if (val === 5) return '5 - Very Unhealthy';
    if (val === 6) return '6 - Hazardous';
    return String(val);
  };

  // ------------------------------------------------------------------
  //  Render
  // ------------------------------------------------------------------

  return (
    <div className="w-full flex flex-col gap-6 animate-fade-in">
      {/* Top: System Status */}
      <div className="glass-panel p-6 flex flex-col md:flex-row items-center justify-between gap-4 border-l-4 border-l-agri-primary">
        <div>
          <h2 className="text-xl font-bold mb-1">System Status</h2>
          <p className="text-emerald-700 text-sm">
            Last sensor update:{' '}
            {lastSensorUpdate
              ? new Date(lastSensorUpdate).toLocaleTimeString()
              : 'Waiting for data...'}
          </p>
        </div>
        <div
          className={`px-6 py-3 rounded-xl flex items-center gap-3 font-semibold text-lg shadow-lg ${
            needsIrrigation
              ? 'bg-amber-50 border border-amber-300 text-amber-600'
              : 'bg-white border border-emerald-400 text-emerald-800'
          }`}
        >
          {needsIrrigation ? (
            <BiXCircle className="text-2xl" />
          ) : (
            <BiCheckCircle className="text-2xl" />
          )}
          {needsIrrigation ? 'Irrigation Recommended' : 'No Irrigation Needed'}
        </div>
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        {/* Left Column: Sensors & Weather */}
        <div className="lg:col-span-1 flex flex-col gap-6">
          {/* Sensor Widget */}
          <div className="glass-panel p-6">
            <h3 className="text-lg font-bold mb-4 flex items-center gap-2">
              <BiTachometer className="text-agri-primary" /> Live Sensors
            </h3>
            <div className="space-y-4">
              <div className="flex justify-between items-center p-3 rounded-xl bg-emerald-50/80 border border-emerald-200/80">
                <div className="flex items-center gap-3 text-emerald-800">
                  <span className="p-2 bg-rose-500/20 text-rose-400 rounded-lg">
                    <BiSun />
                  </span>
                  Temperature
                </div>
                <span className="font-bold text-lg">
                  {temperature !== null ? `${temperature.toFixed(1)}\u00B0C` : '--'}
                </span>
              </div>
              <div className="flex justify-between items-center p-3 rounded-xl bg-emerald-50/80 border border-emerald-200/80">
                <div className="flex items-center gap-3 text-emerald-800">
                  <span className="p-2 bg-blue-500/20 text-blue-400 rounded-lg">
                    <WiHumidity className="text-xl" />
                  </span>
                  Air Humidity
                </div>
                <span className="font-bold text-lg">
                  {humidity !== null ? `${humidity.toFixed(0)}%` : '--'}
                </span>
              </div>
              <div className="flex justify-between items-center p-3 rounded-xl bg-emerald-50/80 border border-emerald-200/80">
                <div className="flex items-center gap-3 text-emerald-800">
                  <span className="p-2 bg-emerald-500/20 text-emerald-400 rounded-lg">
                    <BiDroplet />
                  </span>
                  Soil Moisture
                </div>
                <div className="text-right">
                  <span className="font-bold text-lg">
                    {soilPercent !== null ? `${soilPercent.toFixed(1)}%` : 'Pending...'}
                  </span>
                  {soilUpdated && (
                    <p className="text-xs text-emerald-600">
                      {new Date(soilUpdated).toLocaleString()}
                    </p>
                  )}
                </div>
              </div>
            </div>
            {/* Soil Moisture Progress Bar */}
            <div className="mt-4">
              <div className="flex justify-between text-xs text-emerald-700 mb-1">
                <span>0% (Dry)</span>
                <span>100% (Wet)</span>
              </div>
              <div className="w-full bg-slate-200 rounded-full h-2 overflow-hidden">
                <div
                  className="bg-gradient-to-r from-amber-500 to-emerald-500 h-2 rounded-full transition-all duration-1000"
                  style={{
                    width: `${soilPercent !== null ? Math.min(100, soilPercent) : 0}%`,
                  }}
                />
              </div>
            </div>
          </div>

          {/* Weather Widget */}
          <div className="glass-panel p-6">
            <h3 className="text-lg font-bold mb-4 flex items-center gap-2">
              <BiCloudRain className="text-agri-accent" /> Environmental Details
            </h3>
            <div className="flex items-center justify-between mb-6">
              <div>
                <p className="text-3xl font-bold">
                  {weather ? `${weather.temp_c}\u00B0C` : '--'}
                </p>
                <p className="text-emerald-700 text-sm">
                  {weather ? weather.condition : 'Fetching...'}
                </p>
              </div>
              <div className="text-right">
                <p className="font-semibold text-agri-accent">
                  {weather ? weather.chance_of_rain : '--'}% Rain
                </p>
                <p className="text-emerald-700 text-xs mt-1">Today's Forecast</p>
              </div>
            </div>

            <div className="grid grid-cols-2 gap-3">
              <div
                className={`p-3 rounded-xl bg-emerald-50/80 border border-emerald-200/80 flex justify-between items-center text-sm ${!weather ? 'opacity-50' : ''}`}
              >
                <span className="text-emerald-700">Rain?</span>
                <span
                  className={
                    weather?.will_it_rain
                      ? 'text-agri-accent font-semibold'
                      : 'text-emerald-800'
                  }
                >
                  {weather ? (weather.will_it_rain ? 'Yes' : 'No') : '--'}
                </span>
              </div>
              <div
                className={`p-3 rounded-xl bg-emerald-50/80 border border-emerald-200/80 flex justify-between items-center text-sm ${!weather ? 'opacity-50' : ''}`}
              >
                <span className="text-emerald-700">Wind</span>
                <span className="text-emerald-800 font-semibold">
                  {weather ? `${weather.wind_kph}km/h` : '--'}
                </span>
              </div>
              <div
                className={`p-3 rounded-xl bg-emerald-50/80 border border-emerald-200/80 flex justify-between items-center text-sm ${!weather ? 'opacity-50' : ''}`}
              >
                <span className="text-emerald-700">Humidity</span>
                <span className="text-emerald-800 font-semibold">
                  {weather ? `${weather.humidity}%` : '--'}
                </span>
              </div>
              <div
                className={`p-3 rounded-xl bg-emerald-50/80 border border-emerald-200/80 flex justify-between items-center text-sm ${!weather ? 'opacity-50' : ''}`}
              >
                <span className="text-emerald-700">UV Index</span>
                <span className="text-emerald-800 font-semibold">
                  {weather ? weather.uv_index : '--'}
                </span>
              </div>
              <div
                className={`col-span-2 p-3 rounded-xl bg-emerald-50/80 border border-emerald-200/80 flex justify-between items-center text-sm ${!weather ? 'opacity-50' : ''}`}
              >
                <span className="text-emerald-700">Air Quality Index (EPA)</span>
                <span className="text-emerald-800 font-semibold">
                  {weather ? aqiLabel(weather.aqi_us_epa) : '--'}
                </span>
              </div>
              <div
                className={`col-span-2 p-3 rounded-xl bg-emerald-50/80 border border-emerald-200/80 grid grid-cols-2 gap-x-6 gap-y-3 text-sm ${!weather ? 'opacity-50' : ''}`}
              >
                <div className="flex justify-between items-center border-b border-emerald-100/80 pb-1">
                  <span className="text-emerald-700 font-medium">CO:</span>
                  <span className="text-emerald-800 font-bold text-base">
                    {weather ? (Math.round(weather.aqi_co * 10) / 10) : '--'}{' '}
                    <span className="text-xs font-normal text-emerald-600">
                      ug/m3
                    </span>
                  </span>
                </div>
                <div className="flex justify-between items-center border-b border-emerald-100/80 pb-1">
                  <span className="text-emerald-700 font-medium">NO2:</span>
                  <span className="text-emerald-800 font-bold text-base">
                    {weather ? (Math.round(weather.aqi_no2 * 10) / 10) : '--'}{' '}
                    <span className="text-xs font-normal text-emerald-600">
                      ug/m3
                    </span>
                  </span>
                </div>
                <div className="flex justify-between items-center border-b border-emerald-100/80 pb-1">
                  <span className="text-emerald-700 font-medium">O3:</span>
                  <span className="text-emerald-800 font-bold text-base">
                    {weather ? (Math.round(weather.aqi_o3 * 10) / 10) : '--'}{' '}
                    <span className="text-xs font-normal text-emerald-600">
                      ug/m3
                    </span>
                  </span>
                </div>
                <div className="flex justify-between items-center border-b border-emerald-100/80 pb-1">
                  <span className="text-emerald-700 font-medium">SO2:</span>
                  <span className="text-emerald-800 font-bold text-base">
                    {weather ? (Math.round(weather.aqi_so2 * 10) / 10) : '--'}{' '}
                    <span className="text-xs font-normal text-emerald-600">
                      ug/m3
                    </span>
                  </span>
                </div>
                <div className="flex justify-between items-center pt-1">
                  <span className="text-emerald-700 font-medium">PM2.5:</span>
                  <span className="text-emerald-800 font-bold text-base">
                    {weather ? (Math.round(weather.aqi_pm25 * 10) / 10) : '--'}{' '}
                    <span className="text-xs font-normal text-emerald-600">
                      ug/m3
                    </span>
                  </span>
                </div>
                <div className="flex justify-between items-center pt-1">
                  <span className="text-emerald-700 font-medium">PM10:</span>
                  <span className="text-emerald-800 font-bold text-base">
                    {weather ? (Math.round(weather.aqi_pm10 * 10) / 10) : '--'}{' '}
                    <span className="text-xs font-normal text-emerald-600">
                      ug/m3
                    </span>
                  </span>
                </div>
              </div>
            </div>
          </div>
        </div>

        {/* Right Column: Camera Stream */}
        <div className="lg:col-span-2 flex flex-col h-full">
          <div className="glass-panel p-6 flex-grow flex flex-col">
            <div className="flex justify-between items-center mb-4">
              <h3 className="text-lg font-bold flex items-center gap-2">
                <BiCameraMovie className="text-purple-400" /> Live Camera Feed
              </h3>
              <div className="flex items-center gap-2">
                <span className="relative flex h-3 w-3">
                  <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-red-400 opacity-75" />
                  <span className="relative inline-flex rounded-full h-3 w-3 bg-red-500" />
                </span>
                <span className="text-xs text-red-400 font-semibold tracking-wider font-mono">
                  LIVE
                </span>
              </div>
            </div>

            <div className="w-full flex-grow bg-emerald-900 rounded-xl overflow-hidden border border-emerald-200/80 relative min-h-[300px] flex items-center justify-center group shadow-inner">
              {camURL && !isEditingCam ? (
                <div className="relative w-full h-full">
                  <img
                    src={camURL}
                    alt="ESP32 Camera Stream"
                    className="w-full h-full object-cover"
                    onError={() => setIsEditingCam(true)}
                  />
                  <button
                    onClick={() => setIsEditingCam(true)}
                    className="absolute top-4 right-4 bg-black/60 hover:bg-black/80 text-white p-2 rounded-lg opacity-0 group-hover:opacity-100 transition-opacity flex items-center gap-2 backdrop-blur-md border border-white/20 text-sm"
                  >
                    <BiEdit /> Edit URL
                  </button>
                </div>
              ) : (
                <div className="text-emerald-200/50 flex flex-col items-center p-4 text-center z-10 relative">
                  {camURL ? (
                    <BiWifiOff className="text-6xl mb-2 opacity-80 text-rose-400" />
                  ) : (
                    <BiCameraMovie className="text-6xl mb-2 opacity-50" />
                  )}
                  <p className="mb-2 font-medium text-emerald-100">
                    {camURL
                      ? 'Connection failed or URL changed'
                      : 'No camera configured'}
                  </p>
                  <p className="text-xs text-emerald-600/70 mb-4 max-w-[300px]">
                    Enter the full stream URL for your ESP32 camera (e.g.
                    http://agricam.local:81/stream)
                  </p>
                  <div className="flex gap-2">
                    <input
                      ref={camInputRef}
                      type="text"
                      defaultValue={camURL}
                      placeholder="http://agricam.local:81/stream"
                      className="px-4 py-2 rounded-lg bg-black/40 border border-emerald-500/30 text-emerald-100 placeholder-emerald-800 outline-none focus:border-emerald-500 text-sm w-64 text-center placeholder:text-xs shadow-inner"
                      onKeyDown={(e) => {
                        if (e.key === 'Enter') {
                          const val = (e.target as HTMLInputElement).value.trim();
                          if (val) {
                            saveCamURL(val);
                            setIsEditingCam(false);
                          }
                        }
                      }}
                    />
                    <button
                      onClick={() => {
                        const val = camInputRef.current?.value.trim();
                        if (val) {
                          saveCamURL(val);
                          setIsEditingCam(false);
                        }
                      }}
                      className="px-4 py-2 bg-emerald-600 hover:bg-emerald-500 text-white rounded-lg text-sm font-semibold transition-colors"
                    >
                      Connect
                    </button>
                  </div>
                  {camURL && (
                    <button
                      onClick={() => setIsEditingCam(false)}
                      className="mt-4 text-xs text-emerald-600 hover:text-emerald-400 underline"
                    >
                      Cancel
                    </button>
                  )}
                </div>
              )}
              {/* Corner frame decorations */}
              <div className="absolute top-2 left-2 w-4 h-4 border-t-2 border-l-2 border-white/20" />
              <div className="absolute top-2 right-2 w-4 h-4 border-t-2 border-r-2 border-white/20" />
              <div className="absolute bottom-2 left-2 w-4 h-4 border-b-2 border-l-2 border-white/20" />
              <div className="absolute bottom-2 right-2 w-4 h-4 border-b-2 border-r-2 border-white/20" />
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};
