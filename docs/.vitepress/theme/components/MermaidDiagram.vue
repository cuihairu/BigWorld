<template>
  <figure class="mermaid-panel">
    <figcaption v-if="title" class="mermaid-title">{{ title }}</figcaption>
    <div ref="container" class="mermaid-canvas" />
    <pre ref="source" class="mermaid-slot"><slot /></pre>
    <details v-if="showSource" class="mermaid-source">
      <summary>查看 Mermaid 源码</summary>
      <pre><code>{{ graphText }}</code></pre>
    </details>
  </figure>
</template>

<script setup>
import { nextTick, onMounted, ref, watch } from 'vue'

const props = defineProps({
  title: {
    type: String,
    default: ''
  },
  showSource: {
    type: Boolean,
    default: false
  }
})

const container = ref(null)
const source = ref(null)
const graphText = ref('')

let renderIndex = 0
let mermaidModule = null

async function renderDiagram() {
  if (!container.value) return
  graphText.value = source.value?.textContent?.trim() || ''
  if (!graphText.value) return

  if (!mermaidModule) {
    mermaidModule = (await import('mermaid')).default
    mermaidModule.initialize({
      startOnLoad: false,
      securityLevel: 'strict',
      theme: 'base',
      themeVariables: {
        primaryColor: '#f6e7c9',
        primaryTextColor: '#172018',
        primaryBorderColor: '#8b5e34',
        lineColor: '#53624d',
        secondaryColor: '#d9ead7',
        tertiaryColor: '#f4f0e6',
        fontFamily: 'ui-serif, Georgia, Cambria, serif'
      }
    })
  }

  await nextTick()
  const id = `bw-mermaid-${Date.now()}-${renderIndex++}`
  const { svg } = await mermaidModule.render(id, graphText.value)
  container.value.innerHTML = svg
}

onMounted(renderDiagram)
watch(() => graphText.value, renderDiagram)
</script>
