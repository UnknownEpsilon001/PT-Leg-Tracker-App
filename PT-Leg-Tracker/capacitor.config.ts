import type { CapacitorConfig } from '@capacitor/cli';

const config: CapacitorConfig = {
  appId: 'com.unknownaimo.ptlegtracker',
  appName: 'PT Leg Tracker',
  webDir: 'dist',
  android: {
    allowMixedContent: true, // app is served from https://localhost; device + staff server are plain http on trusted LAN
  },
};

export default config;
