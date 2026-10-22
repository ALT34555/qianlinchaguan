declare global {
  interface Window {
    desktopSettings?: { read(): string | null; write(value: string): void };
    desktopRuntime?: { ready(): void; failed(message: string): void };
  }
}
export {};
