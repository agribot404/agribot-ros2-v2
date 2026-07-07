import React, { useState, useEffect, useCallback } from 'react';
import { BiHistory, BiServer, BiRefresh, BiDownload } from 'react-icons/bi';
import { fetchLogs, type LogEntry } from '../lib/api';

export const Logs: React.FC = () => {
  const [logs, setLogs] = useState<LogEntry[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const loadLogs = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const data = await fetchLogs(200);
      setLogs(data);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to fetch logs');
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    loadLogs();
  }, [loadLogs]);

  const downloadCSV = () => {
    if (logs.length === 0) return;
    
    // Define headers corresponding to LogEntry properties
    const headers = [
      'id', 'timestamp', 'type', 'temperature', 'humidity', 
      'soil_raw', 'soil_percent', 'ph_level', 'condition', 
      'chance_of_rain', 'will_it_rain', 'uv_index', 'wind_kph', 
      'aqi_us_epa', 'aqi_co', 'aqi_no2', 'aqi_o3', 'aqi_so2', 
      'aqi_pm25', 'aqi_pm10'
    ];

    const csvRows = [];
    csvRows.push(headers.join(','));

    for (const log of logs) {
      const values = headers.map(header => {
        const val = log[header as keyof LogEntry];
        return val === null || val === undefined ? '' : String(val);
      });
      csvRows.push(values.join(','));
    }

    const csvData = csvRows.join('\n');
    const blob = new Blob([csvData], { type: 'text/csv' });
    const url = window.URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.setAttribute('hidden', '');
    a.setAttribute('href', url);
    a.setAttribute('download', `agribot_logs_${new Date().toISOString()}.csv`);
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
  };

  return (
    <div className="w-full flex flex-col gap-6 animate-fade-in flex-grow">
      {/* Header */}
      <div className="glass-panel p-6 border-b border-b-agri-primary/30 flex justify-between items-center">
        <div>
          <h2 className="text-xl font-bold flex items-center gap-2">
            <BiHistory className="text-agri-primary" /> Historical Data
          </h2>
          <p className="text-emerald-700 text-sm mt-1">
            Sensor readings logged from the ESP32 over time.
          </p>
        </div>
        <div className="flex items-center gap-3">
          <button
            onClick={downloadCSV}
            disabled={logs.length === 0}
            className="p-2 bg-emerald-100 hover:bg-emerald-200 text-emerald-700 rounded-lg transition-colors disabled:opacity-50 flex items-center gap-1"
            title="Export CSV"
          >
            <BiDownload className="text-xl" /> <span className="text-sm font-semibold hidden sm:inline">CSV</span>
          </button>
          <button
            onClick={loadLogs}
            disabled={loading}
            className="p-2 bg-emerald-100 hover:bg-emerald-200 text-emerald-700 rounded-lg transition-colors disabled:opacity-50"
            title="Refresh"
          >
            <BiRefresh className={`text-xl ${loading ? 'animate-spin' : ''}`} />
          </button>
          <div className="p-3 bg-emerald-50/80 rounded-xl border border-emerald-200/80 flex items-center gap-3">
            <BiServer className="text-emerald-700 text-xl" />
            <span className="text-sm font-semibold">{logs.length} Entries</span>
          </div>
        </div>
      </div>

      {/* Error banner */}
      {error && (
        <div className="glass-panel p-4 border-l-4 border-l-red-400 text-red-700 text-sm">
          {error}
        </div>
      )}

      {/* Table */}
      <div className="glass-panel p-1 flex-grow overflow-hidden flex flex-col">
        <div className="overflow-x-auto">
          <table className="w-full text-left text-sm text-emerald-800">
            <thead className="text-xs text-emerald-700 uppercase bg-emerald-100/80 sticky top-0 border-b border-emerald-200/80">
              <tr>
                <th scope="col" className="px-4 py-4 font-semibold">Date & Time</th>
                <th scope="col" className="px-3 py-4 font-semibold">Type</th>
                <th scope="col" className="px-3 py-4 font-semibold text-center">Temp (&deg;C)</th>
                <th scope="col" className="px-3 py-4 font-semibold text-center">Hum (%)</th>
                <th scope="col" className="px-3 py-4 font-semibold text-center">Soil (%)</th>
                <th scope="col" className="px-3 py-4 font-semibold text-center">pH</th>
                <th scope="col" className="px-3 py-4 font-semibold text-center">Condition</th>
                <th scope="col" className="px-3 py-4 font-semibold text-center">Rain (%)</th>
                <th scope="col" className="px-3 py-4 font-semibold text-center">UV</th>
                <th scope="col" className="px-3 py-4 font-semibold text-center">AQI</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-emerald-200/80">
              {logs.map((log) => (
                <tr key={log.id} className="hover:bg-emerald-50/80 transition-colors">
                  <td className="px-4 py-3 font-mono whitespace-nowrap text-xs">
                    {new Date(log.timestamp).toLocaleString()}
                  </td>
                  <td className="px-3 py-3">
                    <span className={`px-2 py-1 rounded-md text-xs font-semibold ${log.type === 'manual' ? 'bg-purple-500/20 text-purple-600' : 'bg-blue-500/20 text-blue-600'}`}>
                      {log.type.toUpperCase()}
                    </span>
                  </td>
                  <td className="px-3 py-3 text-center font-medium">
                    {log.temperature !== null && log.temperature !== undefined ? log.temperature.toFixed(1) : '--'}
                  </td>
                  <td className="px-3 py-3 text-center font-medium opacity-80">
                    {log.humidity !== null && log.humidity !== undefined ? log.humidity.toFixed(0) : '--'}
                  </td>
                  <td className="px-3 py-3 text-center font-medium">
                    {log.soil_percent !== null && log.soil_percent !== undefined ? (
                      <span className={log.soil_percent < 30 ? 'text-amber-500' : 'text-emerald-500'}>
                        {log.soil_percent.toFixed(1)}
                      </span>
                    ) : '--'}
                  </td>
                  <td className="px-3 py-3 text-center font-medium">
                    {log.ph_level !== null && log.ph_level !== undefined ? log.ph_level.toFixed(2) : '--'}
                  </td>
                  <td className="px-3 py-3 text-center font-medium opacity-80">
                    {log.condition || '--'}
                  </td>
                  <td className="px-3 py-3 text-center font-medium">
                    {log.chance_of_rain !== null && log.chance_of_rain !== undefined ? `${log.chance_of_rain}%` : '--'}
                  </td>
                  <td className="px-3 py-3 text-center font-medium">
                    {log.uv_index !== null && log.uv_index !== undefined ? log.uv_index : '--'}
                  </td>
                  <td className="px-3 py-3 text-center font-medium">
                    {log.aqi_us_epa !== null && log.aqi_us_epa !== undefined ? log.aqi_us_epa : '--'}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
          {loading ? (
            <div className="text-center py-12 text-emerald-700">Loading historical data...</div>
          ) : logs.length === 0 && !error ? (
            <div className="text-center py-12 text-emerald-600">
              No historical data found. Sensor readings will appear here once the ESP32 begins publishing.
            </div>
          ) : null}
        </div>
      </div>
    </div>
  );
};
