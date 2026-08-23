import { getCollection, type CollectionEntry } from "astro:content";

export type DocEntry = CollectionEntry<"docs">;

export async function getSortedDocs(): Promise<DocEntry[]> {
  const docs = await getCollection("docs");
  return docs.sort((a, b) => a.data.order - b.data.order);
}

export function docHref(id: string, sorted: DocEntry[]): string {
  return id === sorted[0]?.id ? "/docs" : `/docs/${id}`;
}
