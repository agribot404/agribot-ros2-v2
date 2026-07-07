/**
 * Type declarations for the `roslib` npm package.
 *
 * roslib does not ship its own TypeScript types. These declarations cover
 * only the subset of the API used by this project.
 */

declare module 'roslib' {
  export class Ros {
    constructor(options?: { url?: string });
    connect(url: string): void;
    close(): void;
    on(event: 'connection' | 'close' | 'error', callback: (event?: unknown) => void): void;
    off(event: 'connection' | 'close' | 'error', callback: (event?: unknown) => void): void;
    get isConnected(): boolean;
  }

  export class Topic {
    constructor(options: { ros: Ros; name: string; messageType: string });
    subscribe(callback: (message: Message) => void): void;
    unsubscribe(callback?: (message: Message) => void): void;
    publish(message: Message): void;
  }

  export class Message {
    constructor(values?: Record<string, unknown>);
    [key: string]: unknown;
  }

  export class Service {
    constructor(options: { ros: Ros; name: string; serviceType: string });
    callService(
      request: ServiceRequest,
      callback: (response: unknown) => void,
      failedCallback?: (error: string) => void,
    ): void;
  }

  export class ServiceRequest {
    constructor(values?: Record<string, unknown>);
  }

  export class ServiceResponse {
    constructor(values?: Record<string, unknown>);
    [key: string]: unknown;
  }
}
