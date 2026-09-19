export default function remarkLocalizeDocLinks() {
  return (tree, file) => {
    const path = (file.path || file.history?.[0] || "").replace(/\\/g, "/");
    const match = path.match(/\/content\/docs\/(en|es|uk)\//);
    if (!match) return;
    const lang = match[1];

    const walk = (node) => {
      if (
        node.type === "link" &&
        typeof node.url === "string" &&
        /^\/(docs|flash)(\/|#|$)/.test(node.url)
      ) {
        node.url = `/${lang}${node.url}`;
      }
      if (Array.isArray(node.children)) node.children.forEach(walk);
    };
    walk(tree);
  };
}
