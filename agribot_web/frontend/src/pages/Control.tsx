import React, { useState, useRef, useCallback, useEffect } from 'react';
import {
  BiJoystick,
  BiUpArrow,
  BiDownArrow,
  BiLeftArrow,
  BiRightArrow,
  BiStopCircle,
  BiDroplet,
  BiSlider,
} from 'react-icons/bi';
import { publishMessage, callService, getRos } from '../lib/ros';
import { postMoistureReading } from '../lib/api';

// ---------------------------------------------------------------------------
//  Constants
// ---------------------------------------------------------------------------

const DRIVE_SPEED = 0.5; // constant linear.x for forward/backward
const TURN_SPEED = 0.5;  // constant angular.z for left/right

// ---------------------------------------------------------------------------
//  Component
// ---------------------------------------------------------------------------

export const Control: React.FC = () => {
  const [rosConnected, setRosConnected] = useState(false);
  const [servoAngle, setServoAngle] = useState(90);
  const [soilResult, setSoilResult] = useState<string | null>(null);
  const [soilLoading, setSoilLoading] = useState(false);
  const lastServoCmd = useRef(0);

  // Track ROS connection state
  useEffect(() => {
    const ros = getRos();
    const onConnect = () => setRosConnected(true);
    const onClose = () => setRosConnected(false);
    const onError = () => setRosConnected(false);

    ros.on('connection', onConnect);
    ros.on('close', onClose);
    ros.on('error', onError);
    if (ros.isConnected) setRosConnected(true);

    return () => {
      ros.off('connection', onConnect);
      ros.off('close', onClose);
      ros.off('error', onError);
    };
  }, []);

  // ------------------------------------------------------------------
  //  Motor control: publish geometry_msgs/Twist to /cmd_vel_teleop
  // ------------------------------------------------------------------

  const sendTwist = useCallback(
    (linear: number, angular: number) => {
      publishMessage('/agribot/cmd_vel_teleop', 'geometry_msgs/msg/Twist', {
        linear: { x: linear, y: 0.0, z: 0.0 },
        angular: { x: 0.0, y: 0.0, z: angular },
      });
    },
    [],
  );

  const stop = useCallback(() => sendTwist(0, 0), [sendTwist]);
  const forward = useCallback(() => sendTwist(DRIVE_SPEED, 0), [sendTwist]);
  const backward = useCallback(() => sendTwist(-DRIVE_SPEED, 0), [sendTwist]);
  const left = useCallback(() => sendTwist(0, TURN_SPEED), [sendTwist]);
  const right = useCallback(() => sendTwist(0, -TURN_SPEED), [sendTwist]);

  // ------------------------------------------------------------------
  //  Servo control: publish std_msgs/Int32 to /cmd_servo
  // ------------------------------------------------------------------

  const sendServo = useCallback((angle: number) => {
    publishMessage('/agribot/cmd_servo', 'std_msgs/msg/Int32', { data: angle });
  }, []);

  // ------------------------------------------------------------------
  //  Soil moisture: call /srv/read_moisture (std_srvs/srv/Trigger)
  // ------------------------------------------------------------------

  const readSoilMoisture = useCallback(async () => {
    setSoilLoading(true);
    setSoilResult(null);
    try {
      const res = await callService<Record<string, never>, { success: boolean; message: string }>(
        '/agribot/srv/read_moisture',
        'std_srvs/srv/Trigger',
        {},
      );
      setSoilResult(res.message);

      // Parse the JSON response and log to backend
      try {
        const parsed = JSON.parse(res.message);
        if (parsed.raw !== undefined && parsed.percent !== undefined) {
          await postMoistureReading({
            raw: parsed.raw,
            percent: parsed.percent,
            ph: parsed.ph !== undefined ? parsed.ph : 7.0,
          });
        }
      } catch {
        // response was not JSON – display it raw
      }
    } catch (err) {
      setSoilResult(`Error: ${err}`);
    } finally {
      setSoilLoading(false);
    }
  }, []);

  const sendSwitch = useCallback((switchId: number, state: boolean) => {
    // switchId: 1 (GPIO 21), 2 (GPIO 22), 3 (GPIO 23)
    // encoding: switchId * 10 + (state ? 1 : 0)
    // e.g. switch 1 ON = 11, OFF = 10
    publishMessage('/agribot/cmd_switch', 'std_msgs/msg/Int32', { data: switchId * 10 + (state ? 1 : 0) });
  }, []);

  // ------------------------------------------------------------------
  //  D-pad button factory (avoids repeating mouse/touch handlers)
  // ------------------------------------------------------------------

  const dpadButton = (
    icon: React.ReactNode,
    onPress: () => void,
    className?: string,
  ) => (
    <button
      onMouseDown={onPress}
      onMouseUp={stop}
      onMouseLeave={stop}
      onTouchStart={(e) => { e.preventDefault(); onPress(); }}
      onTouchEnd={(e) => { e.preventDefault(); stop(); }}
      className={`w-20 h-20 bg-white hover:bg-agri-accent border border-emerald-200/80 text-emerald-900 hover:text-white rounded-2xl shadow-xl flex items-center justify-center transition-all duration-150 active:scale-90 active:bg-blue-400 ${className || ''}`}
    >
      {icon}
    </button>
  );

  // ------------------------------------------------------------------
  //  Render
  // ------------------------------------------------------------------

  return (
    <div className="w-full flex flex-col gap-6 animate-fade-in flex-grow items-center">
      {/* Header */}
      <div className="glass-panel p-6 w-full max-w-2xl text-center">
        <h2 className="text-2xl font-bold flex items-center justify-center gap-3 mb-2">
          <BiJoystick className="text-agri-accent text-3xl" /> Manual Override
        </h2>
        <p className="text-emerald-700 text-sm">
          Direct low-latency control of the AgriBot motors via ROS 2.
        </p>
        <div className="mt-4 p-4 bg-emerald-50/80 rounded-xl border border-emerald-200/80 inline-flex items-center gap-3">
          <span className="text-emerald-800 font-medium">Link Status:</span>
          <span
            className={`px-3 py-1 rounded-md text-xs font-bold uppercase ${
              rosConnected
                ? 'bg-emerald-500/20 text-emerald-600'
                : 'bg-red-500/20 text-red-600'
            }`}
          >
            {rosConnected ? 'Connected' : 'Disconnected'}
          </span>
        </div>
      </div>

      {/* D-Pad + Servo + Soil */}
      <div className="glass-panel p-10 flex flex-col items-center justify-center gap-6 select-none max-w-2xl w-full">
        {/* D-Pad Grid */}
        <div className="grid grid-cols-3 gap-4">
          <div className="col-start-2">
            {dpadButton(<BiUpArrow className="text-3xl" />, forward)}
          </div>
          <div className="col-start-1 row-start-2">
            {dpadButton(<BiLeftArrow className="text-3xl" />, left)}
          </div>
          <div className="col-start-2 row-start-2">
            <button
              onClick={stop}
              className="w-20 h-20 bg-rose-500/80 hover:bg-rose-500 text-white rounded-2xl shadow-xl flex items-center justify-center transition-all duration-150 active:scale-90"
            >
              <BiStopCircle className="text-4xl" />
            </button>
          </div>
          <div className="col-start-3 row-start-2">
            {dpadButton(<BiRightArrow className="text-3xl" />, right)}
          </div>
          <div className="col-start-2 row-start-3">
            {dpadButton(<BiDownArrow className="text-3xl" />, backward)}
          </div>
        </div>

        {/* Servo Slider */}
        <div className="w-full mt-6 bg-white p-6 rounded-xl border border-emerald-200 shadow-sm flex flex-col gap-4">
          <label className="text-emerald-800 font-bold flex items-center gap-2">
            <BiSlider className="text-xl" /> Camera Pan Angle
          </label>
          <div className="flex items-center gap-4">
            <span className="text-xs font-mono text-emerald-600 font-bold">L</span>
            <input
              type="range"
              min="0"
              max="180"
              value={servoAngle}
              onChange={(e) => {
                const val = parseInt(e.target.value);
                setServoAngle(val);
                const now = Date.now();
                // Throttle to 10 Hz
                if (now - lastServoCmd.current > 100) {
                  sendServo(val);
                  lastServoCmd.current = now;
                }
              }}
              onMouseUp={(e) =>
                sendServo(parseInt((e.target as HTMLInputElement).value))
              }
              onTouchEnd={(e) =>
                sendServo(parseInt((e.target as HTMLInputElement).value))
              }
              className="flex-grow h-2 bg-emerald-100 rounded-lg appearance-none cursor-pointer accent-emerald-500"
            />
            <span className="text-xs font-mono text-emerald-600 font-bold">R</span>
            <button
              onClick={() => {
                setServoAngle(90);
                sendServo(90);
              }}
              className="ml-2 px-3 py-1 bg-emerald-100 hover:bg-emerald-200 text-emerald-800 rounded-lg text-xs font-bold transition-colors shadow-sm active:scale-95"
            >
              Center
            </button>
          </div>
          <div className="flex justify-between text-xs text-emerald-600">
            <span>0\u00B0</span>
            <span className="font-bold text-emerald-800">{servoAngle}\u00B0</span>
            <span>180\u00B0</span>
          </div>
        </div>

        {/* Reserved Switches */}
        <div className="w-full mt-6 bg-white p-6 rounded-xl border border-emerald-200 shadow-sm flex flex-col gap-4">
          <label className="text-emerald-800 font-bold flex items-center gap-2">
            Reserved Momentary Outputs (GPIO 21, 22, 23)
          </label>
          <div className="flex justify-around gap-4 mt-2">
            {[
              { id: 1, label: 'Pump' },
              { id: 2, label: 'Saw' },
              { id: 3, label: 'Probe' }
            ].map(({ id, label }) => (
              <button
                key={id}
                onMouseDown={() => sendSwitch(id, true)}
                onMouseUp={() => sendSwitch(id, false)}
                onMouseLeave={() => sendSwitch(id, false)}
                onTouchStart={(e) => { e.preventDefault(); sendSwitch(id, true); }}
                onTouchEnd={(e) => { e.preventDefault(); sendSwitch(id, false); }}
                className="w-24 h-24 bg-emerald-100 hover:bg-emerald-500 text-emerald-800 hover:text-white rounded-2xl shadow-md flex flex-col items-center justify-center transition-all duration-150 active:scale-95 active:bg-emerald-600 font-bold"
              >
                <span className="capitalize">{label}</span>
              </button>
            ))}
          </div>
        </div>

        {/* Soil Moisture Service */}
        <div className="flex flex-wrap gap-4 mt-8 w-full justify-center border-t border-emerald-100/50 pt-8">
          <button
            onClick={readSoilMoisture}
            disabled={soilLoading}
            className="px-6 py-3 bg-amber-400 hover:bg-amber-300 border border-amber-300 text-amber-950 font-bold rounded-xl shadow-lg flex items-center gap-2 transition-all duration-150 active:scale-95 disabled:opacity-50 disabled:cursor-not-allowed"
          >
            <BiDroplet className="text-2xl text-amber-700" />
            {soilLoading ? 'Reading...' : 'Measure Soil Moisture'}
          </button>

          {soilResult && (
            <div className="w-full mt-2 p-4 bg-white rounded-xl border border-emerald-200 shadow-sm text-center">
              <p className="text-sm text-emerald-700 font-mono">{soilResult}</p>
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
