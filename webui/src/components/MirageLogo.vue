<script setup lang="ts">
defineProps<{
  size?: number;
}>();
</script>

<template>
  <img
    src="./mirageforge-logo.svg"
    :width="size || 44"
    :height="size || 44"
    alt="MirageForge Logo"
    class="mirage-logo-img"
    draggable="false"
  />
</template>

<style scoped>
.mirage-logo-img {
  display: block;
  flex-shrink: 0;
  border-radius: 13px;
  filter: drop-shadow(0 6px 14px rgba(37, 99, 235, 0.35));
  transition: transform 0.35s cubic-bezier(0.34, 1.56, 0.64, 1);
}
.mirage-logo-img:active {
  transform: scale(0.9) rotate(-4deg);
}
</style>
