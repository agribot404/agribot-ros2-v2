import React, { useEffect, useState } from 'react';
import { NavLink } from 'react-router-dom';
import { BiLeaf, BiTachometer, BiHistory, BiJoystick, BiWifi, BiWifiOff } from 'react-icons/bi';
import { getRos } from '../lib/ros';

export const Navbar: React.FC = () => {
  const [rosConnected, setRosConnected] = useState(false);

  useEffect(() => {
    const ros = getRos();

    const onConnect = () => setRosConnected(true);
    const onClose = () => setRosConnected(false);
    const onError = () => setRosConnected(false);

    ros.on('connection', onConnect);
    ros.on('close', onClose);
    ros.on('error', onError);

    // Check current state (may already be connected)
    if (ros.isConnected) setRosConnected(true);

    return () => {
      ros.off('connection', onConnect);
      ros.off('close', onClose);
      ros.off('error', onError);
    };
  }, []);

  return (
    <nav className="glass-panel w-full p-4 mb-8 flex flex-col sm:flex-row justify-between items-center gap-4">
      <div className="flex items-center gap-3">
        <div className="bg-agri-primary/20 p-2 rounded-full ring-1 ring-agri-primary/50">
          <BiLeaf className="text-2xl text-agri-primary drop-shadow-[0_0_8px_rgba(16,185,129,0.5)]" />
        </div>
        <h1 className="text-xl font-bold bg-gradient-to-r from-emerald-600 to-teal-600 bg-clip-text text-transparent">
          AgriBot Dashboard
        </h1>
      </div>

      <div className="flex items-center gap-6">
        <NavLink
          to="/"
          className={({ isActive }) =>
            `flex items-center gap-2 font-medium transition-colors ${isActive ? 'text-agri-primary' : 'text-emerald-700 hover:text-emerald-900'}`
          }
          end
        >
          <BiTachometer className="text-xl" /> Status
        </NavLink>
        <NavLink
          to="/control"
          className={({ isActive }) =>
            `flex items-center gap-2 font-medium transition-colors ${isActive ? 'text-agri-primary' : 'text-emerald-700 hover:text-emerald-900'}`
          }
        >
          <BiJoystick className="text-xl" /> Control
        </NavLink>
        <NavLink
          to="/logs"
          className={({ isActive }) =>
            `flex items-center gap-2 font-medium transition-colors ${isActive ? 'text-agri-primary' : 'text-emerald-700 hover:text-emerald-900'}`
          }
        >
          <BiHistory className="text-xl" /> Logs
        </NavLink>

        <div className="h-6 w-px bg-emerald-200 mx-2" />

        {/* ROS connection indicator */}
        <div className="flex items-center gap-2">
          {rosConnected ? (
            <BiWifi className="text-xl text-emerald-500" />
          ) : (
            <BiWifiOff className="text-xl text-red-400" />
          )}
          <span
            className={`px-2 py-1 rounded-md text-xs font-bold uppercase ${
              rosConnected
                ? 'bg-emerald-500/20 text-emerald-600'
                : 'bg-red-500/20 text-red-600'
            }`}
          >
            {rosConnected ? 'ROS' : 'Offline'}
          </span>
        </div>
      </div>
    </nav>
  );
};
