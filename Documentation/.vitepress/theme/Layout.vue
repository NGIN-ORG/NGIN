<script setup lang="ts">
import DefaultTheme from "vitepress/theme";
import PageActions from "./PageActions.vue";

const { Layout } = DefaultTheme;
</script>

<template>
  <Layout>
    <template #doc-before>
      <PageActions />
    </template>
  </Layout>
</template>
