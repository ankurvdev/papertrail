<script lang="ts">
	import { default as Button, Icon } from '@smui/button';
	import { default as Card } from '@smui/card';
	import { scale } from 'svelte/transition';
	import { expoInOut } from 'svelte/easing';
	import type { SearchResultItemType } from './store';
	export let searchResultItem: SearchResultItemType;
</script>

<Card variant="outlined" padded>
	<div class="item">
		<a class="mdc-typography--body2" href="{searchResultItem.url}">{searchResultItem.filename}</a>
		<div class="mdc-typography--caption" bind:innerHTML={searchResultItem.snippet} contenteditable />
	</div>
</Card>

<style lang="scss">
	@use '@material/typography/mdc-typography';
	.item {
		overflow-wrap: break-word;
	}
</style>