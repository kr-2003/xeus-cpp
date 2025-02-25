import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: './src',
  timeout: 60000,
  retries: 1,
  use: {
    baseURL: 'http://localhost:8000',
    headless: true, // Set to false for debugging
    screenshot: 'only-on-failure',
    video: 'on',
  },
  projects: [
    {
      name: 'chromium',
      use: { browserName: 'chromium' },
    },
  ],
});