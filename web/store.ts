import { writable, type Writable } from "svelte/store"

export interface SearchResultItemType {
    snippet: string
    contents: string
    url: string
    filename: string
}

const EmptySearchResultItemType: SearchResultItemType = {
    snippet: "",
    contents: "",
    url: "",
    filename: ""
}

export const vm_selectedIds: Writable<{ [item_id: string]: boolean }> = writable({})

export async function SearchTypesense(searchTerm: string): Promise<SearchResultItemType[]> {
    let searchParameters = {
        'q': searchTerm,
        'query_by': 'contents,filename',
        //'filter_by' : 'num_employees:>100',
        //'sort_by'   : 'num_employees:desc'
    }
    const response = await fetch('/search?' + new URLSearchParams(searchParameters))
    const searchResults = await response.json();
    let results: SearchResultItemType[] = []
    if (searchResults.hits && searchResults.hits.length > 0) {
        let hits = searchResults.hits
        hits.forEach((hit) => {
            let searchResultItem: SearchResultItemType = { ...EmptySearchResultItemType }
            searchResultItem.snippet = hit.highlight.contents.snippet
            Object.assign(searchResultItem, hit.document);
            results.push(searchResultItem)
        });
    }
    return results
};
