import { writable, type Writable } from "svelte/store"
import Typesense from "typesense"

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
const typesenseclient = new Typesense.Client({
    nodes: [
        {
            host: 'localhost',
            port: 8108,
            protocol: 'http'
        },
    ],
    apiKey: 'test',
    numRetries: 3, // A total of 4 tries (1 original try + 3 retries)
    connectionTimeoutSeconds: 10,
    logLevel: 'debug'
});

export async function SearchTypesense(searchTerm: string): Promise<SearchResultItemType[]> {
    let searchParameters = {
        'q': searchTerm,
        'query_by': 'contents,filename',
        //'filter_by' : 'num_employees:>100',
        //'sort_by'   : 'num_employees:desc'
    }

    let searchResults = await typesenseclient.collections('documents').documents().search(searchParameters);
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
