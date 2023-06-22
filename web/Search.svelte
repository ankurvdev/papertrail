<script lang="ts">
	import Button from '@smui/button';
	import DataTable, { Body, Cell, Head, Label, Row, SortValue } from '@smui/data-table';
	import IconButton, { Icon } from '@smui/icon-button';
	import MediaContentItemView from './MediaContentItemView.svelte';
	import {
		archive,
		setupEventSource,
		source_add,
		vm_selectedIds,
		vm_sources,
		vm_folders,
		type File,
		type Source,
		type ViewModelFolder
	} from './store';
	let content_group_map: { [id: string]: ViewModelFolder } = {};
	let source_map: { [id: string]: Source } = {};
	let newsrctoadd: string = '';

	let selectedItem: ViewModelFolder | null = null;
	let sortDirection: Lowercase<keyof typeof SortValue> = 'ascending';
	let sortKeyFolder = 'title';
	let sortKeySource = 'path';

	let getSortValueForFolder = (item: ViewModelFolder) => {
		return item.info.title_id;
	};
	let getSortValueForSource = (item: Source) => {
		return item.path;
	};

	let clicked = (item: ViewModelFolder) => {
		if (selectedItem == item) {
			selectedItem = null;
		} else {
			selectedItem = item;
		}
		let selected = item.tag.__id in $vm_selectedIds;
		if (!selected) {
			$vm_selectedIds[item.tag.__id] = true;
		} else {
			delete $vm_selectedIds[item.tag.__id];
		}
	};

	function handleSort() {
		let items = Object.values(content_group_map);
		items.sort((a, b) => {
			const [aVal, bVal] = [getSortValueForFolder(a), getSortValueForFolder(b)][
				sortDirection === 'ascending' ? 'slice' : 'reverse'
			]();
			if (typeof aVal === 'string' && typeof bVal === 'string') {
				return aVal.localeCompare(bVal);
			}
			return Number(aVal) - Number(bVal);
		});
		$vm_folders = items;
	}

	function handleSourceSort() {
		let sources = Object.values(source_map);
		sources.sort((a, b) => {
			const [aVal, bVal] = [getSortValueForSource(a), getSortValueForSource(b)][
				sortDirection === 'ascending' ? 'slice' : 'reverse'
			]();
			if (typeof aVal === 'string' && typeof bVal === 'string') {
				return aVal.localeCompare(bVal);
			}
			return Number(aVal) - Number(bVal);
		});
		$vm_sources = sources;
	}

	function basename(url: string) {
		if (!url || url.length == 0) return null;
		return new URL(url).pathname
			.split('/')
			.reverse()
			.filter((obj) => {
				return obj.length > 0;
			})[0];
	}

	function group_delete(o: ViewModelFolder) {
		if (o.tag.__id in content_group_map) {
			delete content_group_map[o.tag.__id];
		}
		handleSort();
	}

	function group_updated(o: ViewModelFolder) {
		if (o.tag.__id in content_group_map) {
			Object.assign(content_group_map[o.tag.__id], o);
		} else {
			content_group_map[o.tag.__id] = o;
		}
		handleSort();
	}

	function source_delete(o: Source) {
		if (o.__id in source_map) {
			delete source_map[o.__id];
		}
		handleSourceSort();
	}

	function source_updated(o: Source) {
		if (o.__id in source_map) {
			Object.assign(source_map[o.__id], o);
		} else {
			source_map[o.__id] = o;
		}
		handleSourceSort();
	}

	let g_evtsrc = setupEventSource();

	function showoverlay(item: ViewModelFolder) {
		selectedItem = item;
	}
	function hideoverlay(item: ViewModelFolder) {
		selectedItem = null;
	}

	function get_group_content_indices(item: ViewModelFolder): number[] {
		let indices: number[] = [];
		Object.entries(item.remote_files).forEach((k) => {
			indices.push(k[1].__id);
		});
		/*
		Object.entries(item.subgroups).forEach((k) => {
			indices = indices.concat(get_group_content_indices(k[1]));
		});*/
		return indices;
	}

	function bulkarchive(item: ViewModelFolder) {
		let index = $vm_folders.indexOf(item);
		let indices: number[] = [];
		while (index >= 0) {
			indices = indices.concat(get_group_content_indices($vm_folders[index]));
			index--;
		}
		archive(indices);
	}
</script>

<input bind:value={newsrctoadd} /><IconButton
	class="material-icons"
	on:click={() => source_add(newsrctoadd)}
	touch>add</IconButton
>

<DataTable
	sortable
	bind:sort={sortKeyFolder}
	bind:sortDirection
	on:SMUIDataTable:sorted={handleSort}
	table$aria-label="User list"
	style="width: 100%;"
>
	<Head>
		<Row>
			<!--
			Note: whatever you supply to "columnId" is
			appended with "-status-label" and used as an ID
			for the hidden label that describes the sort
			status to screen readers.
			You can localize those labels with the
			"sortAscendingAriaLabel" and
			"sortDescendingAriaLabel" props on the DataTable.
			-->
			<Cell columnId="name" style="width: 100%;">
				<Label>Name</Label>
				<!-- For non-numeric columns, icon comes second. -->
				<IconButton class="material-icons">arrow_upward</IconButton>
			</Cell>
			<Cell columnId="rating">
				<Label>Rating</Label>
				<IconButton class="material-icons">arrow_upward</IconButton>
			</Cell>
			<Cell columnId="votes">
				<Label>Votes</Label>
				<IconButton class="material-icons">arrow_upward</IconButton>
			</Cell>
		</Row>
	</Head>
	<Body>
		{#each $vm_folders as item (item.tag.__id)}
			<Row
				on:click={() => clicked(item)}
				on:mouseenter={() => showoverlay(item)}
				on:mouseleave={() => hideoverlay(item)}
			>
				<Cell>
					<MediaContentItemView group={item} />
				</Cell>
				<Cell>
					{item.info.rating}
				</Cell>
				<Cell>
					{#if item == selectedItem}
						<Button on:click={() => bulkarchive(item)}
							><Icon class="material-icons">clear</Icon></Button
						>
					{:else}
						{item.info.votes}
					{/if}
				</Cell>
			</Row>
		{/each}
	</Body>
</DataTable>
<DataTable
	sortable
	bind:sort={sortKeySource}
	bind:sortDirection
	on:SMUIDataTable:sorted={handleSourceSort}
	table$aria-label="User list"
	style="width: 100%;"
>
	<Head>
		<Row>
			<Cell columnId="id">
				<Label>Id</Label>
				<!-- For non-numeric columns, icon comes second. -->
				<IconButton class="material-icons">arrow_upward</IconButton>
			</Cell>
			<Cell columnId="url" style="width: 100%;">
				<Label>URL</Label>
				<IconButton class="material-icons">arrow_upward</IconButton>
			</Cell>
			<Cell columnId="frequency">
				<Label>Frequency</Label>
				<IconButton class="material-icons">arrow_upward</IconButton>
			</Cell>
			<Cell columnId="lastchecked">
				<Label>Last Checked</Label>
				<IconButton class="material-icons">arrow_upward</IconButton>
			</Cell>
			<Cell columnId="message">
				<Label>Status Info</Label>
				<IconButton class="material-icons">arrow_upward</IconButton>
			</Cell>
		</Row>
	</Head>
	<Body>
		{#each $vm_sources as src (src.__id)}
			<Row>
				<Cell>{src.__id}</Cell>
				<Cell>{src.path}</Cell>
				<Cell>{src.frequencyInSecs}</Cell>
				<Cell>{src.last_checked}</Cell>
				<Cell>{src.status_info}</Cell>
			</Row>
		{/each}
	</Body>
</DataTable>
