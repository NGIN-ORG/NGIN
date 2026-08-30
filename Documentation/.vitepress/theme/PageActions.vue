<script setup lang="ts">
import { computed, ref, watch } from "vue";
import { useData, withBase } from "vitepress";

const { page, frontmatter } = useData();
const copied = ref(false);
const failed = ref(false);
const sourceUrl = computed(() => withBase(`/raw/${page.value.relativePath}`));
const visible = computed(() => frontmatter.value.layout !== "home");

watch(() => page.value.relativePath, () => {
  copied.value = false;
  failed.value = false;
});

async function copyMarkdown() {
  copied.value = false;
  failed.value = false;
  try {
    const response = await fetch(sourceUrl.value);
    if (!response.ok) {
      throw new Error(`Unable to read ${sourceUrl.value}`);
    }
    await navigator.clipboard.writeText(await response.text());
    copied.value = true;
    window.setTimeout(() => { copied.value = false; }, 1800);
  } catch {
    failed.value = true;
  }
}
</script>

<template>
  <div v-if="visible" class="page-actions" aria-label="Page actions">
    <span class="page-actions__label">Markdown source</span>
    <div class="page-actions__buttons">
      <button class="page-action" type="button" @click="copyMarkdown">
        <svg aria-hidden="true" viewBox="0 0 24 24"><path d="M8 7V5a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2h-2M6 7h8a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V9a2 2 0 0 1 2-2Z"/></svg>
        {{ copied ? "Copied" : failed ? "Copy failed" : "Copy page" }}
      </button>
      <a class="page-action" :href="sourceUrl" target="_blank" rel="noopener">
        <svg aria-hidden="true" viewBox="0 0 24 24"><path d="M14 3h7v7M21 3l-9 9M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/></svg>
        View raw
      </a>
    </div>
  </div>
</template>
