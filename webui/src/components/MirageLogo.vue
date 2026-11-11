<script setup lang="ts">
defineProps<{
  size?: number;
}>();

// 使用常量绑定给 :src，绕过 Vite 模板编译器的静态 import 转换，
// 运行时直接加载由 webui/public/mirageforge-logo.svg 拷贝到 webroot 根目录的图标
const logoPath = './mirageforge-logo.svg';
</script>

<template>
  <img
    :src="logoPath"
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
