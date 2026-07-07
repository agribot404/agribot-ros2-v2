/**
 * ros.ts – roslibjs connection singleton and reusable hooks.
 *
 * Provides a single ROSLIB.Ros instance that auto-reconnects, plus thin
 * wrappers for subscribing, publishing, and calling services.
 */

import ROSLIB from 'roslib';

// ---------------------------------------------------------------------------
//  Connection singleton
// ---------------------------------------------------------------------------

// Fallback to the current browser hostname so it works when accessed from other devices on the network
const envUrl = import.meta.env.VITE_ROSBRIDGE_URL;
const ROSBRIDGE_URL = envUrl && !envUrl.includes('localhost') 
  ? envUrl 
  : `ws://${window.location.hostname}:9090`;

let rosInstance: ROSLIB.Ros | null = null;

export function getRos(): ROSLIB.Ros {
  if (rosInstance) return rosInstance;

  rosInstance = new ROSLIB.Ros({ url: ROSBRIDGE_URL });

  rosInstance.on('connection', () => {
    console.log('[ROS] Connected to rosbridge at', ROSBRIDGE_URL);
  });

  rosInstance.on('error', (err) => {
    console.error('[ROS] Connection error:', err);
  });

  rosInstance.on('close', () => {
    console.warn('[ROS] Connection closed – will retry in 3 s');
    setTimeout(() => {
      if (rosInstance) {
        try {
          rosInstance.connect(ROSBRIDGE_URL);
        } catch {
          // swallow – the error callback will fire
        }
      }
    }, 3000);
  });

  return rosInstance;
}

// ---------------------------------------------------------------------------
//  Helper: subscribe to a topic
// ---------------------------------------------------------------------------

export function subscribeTopic<T>(
  topicName: string,
  messageType: string,
  callback: (msg: T) => void,
): ROSLIB.Topic {
  const topic = new ROSLIB.Topic({
    ros: getRos(),
    name: topicName,
    messageType,
  });
  topic.subscribe(callback as (msg: ROSLIB.Message) => void);
  return topic;
}

// ---------------------------------------------------------------------------
//  Helper: publish to a topic
// ---------------------------------------------------------------------------

export function publishMessage(
  topicName: string,
  messageType: string,
  data: Record<string, unknown>,
): void {
  const topic = new ROSLIB.Topic({
    ros: getRos(),
    name: topicName,
    messageType,
  });
  const msg = new ROSLIB.Message(data);
  topic.publish(msg);
}

// ---------------------------------------------------------------------------
//  Helper: call a service
// ---------------------------------------------------------------------------

export function callService<TReq, TRes>(
  serviceName: string,
  serviceType: string,
  request: TReq,
): Promise<TRes> {
  return new Promise((resolve, reject) => {
    const srv = new ROSLIB.Service({
      ros: getRos(),
      name: serviceName,
      serviceType,
    });
    const req = new ROSLIB.ServiceRequest(request as Record<string, unknown>);
    srv.callService(req, (res) => resolve(res as TRes), (err) => reject(err));
  });
}
