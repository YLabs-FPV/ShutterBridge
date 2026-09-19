import { getCollection, type CollectionEntry } from "astro:content";

export type DocEntry = CollectionEntry<"docs">;

export function docLang(id: string): string {
  return id.split("/")[0];
}

export function docSlug(id: string): string {
  return id.split("/").slice(1).join("/");
}

export async function getSortedDocs(lang: string): Promise<DocEntry[]> {
  const docs = await getCollection("docs");
  return docs
    .filter((d) => docLang(d.id) === lang)
    .sort((a, b) => a.data.order - b.data.order);
}

export function docHref(lang: string, id: string, sorted: DocEntry[]): string {
  const base = `/${lang}/docs`;
  return id === sorted[0]?.id ? base : `${base}/${docSlug(id)}`;
}
