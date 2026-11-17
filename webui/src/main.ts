import { createApp } from 'vue';
import App from './App.vue';
import './styles/liquid-glass.css';
import {
  initLiquidLensNormalMap,
  registerToastHandler,
  vTouchGlass,
} from './bridge/ksu-io';
import { applyThemeMode, triggerLiquidToast } from './store/mirageStore';

registerToastHandler((msg: string) => {
  triggerLiquidToast(msg);
});

const app = createApp(App);
app.directive('touch-glass', vTouchGlass);
app.mount('#app');

// 挂载完成后再初始化主题与光学折射贴图，确保任何 WebView 差异都不会阻塞界面渲染
requestAnimationFrame(() => {
  try {
    applyThemeMode();
  } catch (_) {}
  try {
    initLiquidLensNormalMap();
  } catch (_) {}
});
