import { createApp } from 'vue';
import App from './App.vue';
import './styles/liquid-glass.css';
import {
  initImmersionMode,
  registerToastHandler,
  vTouchGlass,
} from './bridge/webui-x';
import { triggerLiquidToast } from './store/mirageStore';

initImmersionMode();
registerToastHandler((msg: string) => {
  triggerLiquidToast(msg);
});

const app = createApp(App);
app.directive('touch-glass', vTouchGlass);
app.mount('#app');
