// @ts-check
import { defineConfig } from 'astro/config';
import tailwindcss from '@tailwindcss/vite';
import svelte from '@astrojs/svelte';
import icon from 'astro-icon';
import sitemap from '@astrojs/sitemap';
import mdx from '@astrojs/mdx';

import cloudflare from '@astrojs/cloudflare';

export default defineConfig({
  site: 'https://shutterbridge.yarosfpv.com',
  compressHTML: true,
  integrations: [svelte(), icon(), sitemap(), mdx()],

  vite: {
    plugins: [tailwindcss()],
    optimizeDeps: { include: ['esptool-js'] },
  },

  adapter: cloudflare(),
});