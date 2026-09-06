import type { APIRoute } from "astro";
import { GITHUB_URL } from "../../consts";

export const prerender = false;

const ALLOWED_PREFIX = `${GITHUB_URL}/releases/download/`;

export const GET: APIRoute = async ({ url }) => {
  const target = url.searchParams.get("url");

  if (!target || !target.startsWith(ALLOWED_PREFIX)) {
    return new Response("Invalid asset URL", { status: 400 });
  }

  const upstream = await fetch(target, {
    headers: { Accept: "application/octet-stream" },
    redirect: "follow",
  });

  if (!upstream.ok || !upstream.body) {
    return new Response(`Upstream error (${upstream.status})`, { status: 502 });
  }

  return new Response(upstream.body, {
    status: 200,
    headers: {
      "Content-Type": "application/octet-stream",
      "Cache-Control": "public, max-age=3600",
    },
  });
};
