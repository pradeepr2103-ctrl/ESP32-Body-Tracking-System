import { defineConfig } from 'vite';
import path from 'path';

const PROTOCOL_SRC = path.resolve(__dirname, '../solarxr-protocol/protocol/typescript/src');

export default defineConfig({
  resolve: {
    alias: {
      'solarxr-protocol': path.join(PROTOCOL_SRC, 'all_generated.ts'),
      flatbuffers: path.resolve(__dirname, 'node_modules/flatbuffers/mjs/index.js'),
    },
  },
  optimizeDeps: {
    include: ['solarxr-protocol', 'flatbuffers'],
  },
  build: {
    target: 'es2022',
  },
});
