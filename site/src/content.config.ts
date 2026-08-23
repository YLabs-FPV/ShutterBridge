import { defineCollection } from "astro:content";
import { glob } from "astro/loaders";
import { z } from "astro/zod";

const docs = defineCollection({
  loader: glob({
    base: "./src/content/docs",
    pattern: ["**/*.{md,mdx}", "!**/_*.{md,mdx}"],
  }),
  schema: () =>
    z.object({
      title: z.string(),
      description: z.string().optional(),
      order: z.number().default(100),
    }),
});

export const collections = { docs };
