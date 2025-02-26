import { test, expect } from '@playwright/test';
import * as path from 'path';
import { execSync } from 'child_process';


const JUPYTERLITE_URL = 'http://localhost:8000';

test.beforeAll(async () => {
  console.log('Building and starting JupyterLite with xeus-cpp...');
  try {
    execSync(`${path.join(__dirname, '..', 'build-jupyterlite.sh')}`, { stdio: 'inherit' });
  } catch (error) {
    console.error('Failed to build/start JupyterLite:', error.message);
    throw error;
  }
  await new Promise(resolve => setTimeout(resolve, 10000));
});

test.describe('xeus-cpp-lite UI Test', () => {
  test('xeus-cpp-lite should execute some code', async ({ page }) => {

    await page.goto(`${JUPYTERLITE_URL}/lab/index.html`, { timeout: 10000 });

    await page.waitForSelector('.jp-LauncherCard', { timeout: 10000 });

    await page.locator('.jp-LauncherCard[title*="C++20"]').first().click();

    await page.waitForSelector('.jp-Notebook', { timeout: 10000 });

    const code = `
      #include <iostream>
      std::cout << "Hello World" << std::endl;
    `;
    await page.locator('.jp-CodeMirrorEditor').first().click();
    await page.keyboard.type(code);

    await page.keyboard.press('Control+Enter');

    await page.waitForSelector('.jp-OutputArea-output', { timeout: 20000 });
    await page.waitForTimeout(2000); // Additional wait for execution

    const outputArea = page.locator('.jp-OutputArea-output').first();
    const screenshot = await outputArea.screenshot();

    expect(screenshot).toMatchSnapshot('xeus-cpp-hello-output.png');
  });

  test('xeus-cpp-lite should inspect documentation', async ({ page }) => {
    
    await page.goto(`${JUPYTERLITE_URL}/lab/index.html`, { timeout: 10000 });

    await page.waitForSelector('.jp-LauncherCard', { timeout: 10000 });

    await page.locator('.jp-LauncherCard[title*="C++20"]').first().click();

    await page.waitForSelector('.jp-Notebook', { timeout: 10000 });

    const code = `?std::vector`;
    await page.locator('.jp-CodeMirrorEditor').first().click();
    await page.keyboard.type(code);

    await page.keyboard.press('Control+Enter');

    await page.waitForSelector('.jp-OutputArea-output', { timeout: 20000 });
    await page.waitForTimeout(2000); // Additional wait for execution

    const outputArea = page.locator('.jp-OutputArea-output').first();
    const screenshot = await outputArea.screenshot();

    expect(screenshot).toMatchSnapshot('xeus-cpp-inspect-output.png');
  });
});

test.afterAll(async () => {
  console.log('Shutting down JupyterLite server...');
  try {
    execSync('pkill -f "jupyter lite serve" || true', { stdio: 'inherit' });
    await new Promise(resolve => setTimeout(resolve, 2000));
  } catch (error) {
    console.warn('Failed to shut down server (might already be stopped):', error.message);
  }
});