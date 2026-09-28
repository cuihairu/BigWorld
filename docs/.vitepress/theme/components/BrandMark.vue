<script setup>
import { withBase } from 'vitepress'
</script>

<template>
  <div class="bw-brand-mark">
    <img
      :src="withBase('/logo.svg')"
      alt="BigWorld logo"
      width="108"
      height="108"
    />
  </div>
</template>
