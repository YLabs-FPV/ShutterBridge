// @ts-check
import { defineConfig } from 'astro/config';
import tailwindcss from '@tailwindcss/vite';
import svelte from '@astrojs/svelte';
import icon from 'astro-icon';
import sitemap from '@astrojs/sitemap';
import mdx from '@astrojs/mdx';

import cloudflare from '@astrojs/cloudflare';
import remarkLocalizeDocLinks from './src/lib/remark-localize-doc-links.mjs';

export default defineConfig({
  site: 'https://shutterbridge.yarosfpv.com',
  compressHTML: true,

  markdown: {
    remarkPlugins: [remarkLocalizeDocLinks],
  },
  integrations: [
    svelte(),
    icon(),
    sitemap({
      i18n: {
        defaultLocale: 'en',
        locales: { en: 'en', es: 'es', uk: 'uk' },
      },
    }),
    mdx(),
  ],

  i18n: {
    defaultLocale: 'en',
    locales: ['en', 'es', 'uk'],
    routing: {
      prefixDefaultLocale: true,
    },
  },

  vite: {
    plugins: [tailwindcss()],
    // Pre-bundle the deps that would otherwise be discovered mid-session and
    // trigger a full optimizer reload, which the Cloudflare workerd dev runner
    // can't survive (crash: ".vite/deps_ssr/handler-*.js does not exist").
    optimizeDeps: {
      include: [
        'esptool-js',
        '@astrojs/svelte/server.js',
        'astro-icon/components',
        'astro/logger/console',
      ],
    },
  },

  adapter: cloudflare(),
});