<script lang="ts">
	import Button from '@smui/button';
	import IconButton, { Icon } from '@smui/icon-button';
	import SearchResultItem from './SearchResultItem.svelte';
	import {
	} from './store';

</script>

<input bind:value={newsrctoadd} /><IconButton
	class="material-icons"
	on:click={() => source_add(newsrctoadd)}
	touch>search</IconButton
>
