import type { CapacitorConfig } from '@capacitor/cli';

const config: CapacitorConfig = {
  appId: 'com.unknownaimo.ptlegtracker',
  appName: 'Med-Flex Auto Smart',
  webDir: 'dist',
  android: {
    allowMixedContent: true, // app is served from https://localhost; device + staff server are plain http on trusted LAN
  },
};

export default config;
