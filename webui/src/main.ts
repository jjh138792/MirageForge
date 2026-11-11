import { createApp } from 'vue';
import App from './App.vue';
import './styles/liquid-glass.css';
import {
  initLiquidLensNormalMap,
  registerToastHandler,
  vTouchGlass,
} from './bridge/ksu-io';
import { applyThemeMode, triggerLiquidToast } from './store/mirageStore';

// 1. 初始化日/夜/跟随系统主题
applyThemeMode();

// 2. 注册全局 Toast 与液态透镜法线贴图
registerToastHandler((msg: string) => {
  triggerLiquidToast(msg);
});

const app = createApp(App);
app.directive('touch-glass', vTouchGlass);
app.mount('#app');

// 3. 在 DOM 挂载后注入 SVG 球面光学折射位移贴图
requestAnimationFrame(() => {
  initLiquidLensNormalMap();
});
