import { h } from 'vue'
import DefaultTheme from 'vitepress/theme'
import MermaidDiagram from './components/MermaidDiagram.vue'
import BrandMark from './components/BrandMark.vue'
import './style.css'

export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    app.component('MermaidDiagram', MermaidDiagram)
  },
  Layout() {
    return h(DefaultTheme.Layout, null, {
      // Centered brand mark above the home hero title.
      'home-hero-before': () => h(BrandMark)
    })
  }
}
