import { writable, type Writable } from "svelte/store"
export const vm_selectedIds: Writable<{ [item_id: string]: boolean }> = writable({})

export function archive(ids: number[]) {
    const idstr = JSON.stringify(ids)
    const xhr = new XMLHttpRequest()
    xhr.open('GET', `${URL_ROOT}/archive?ids=${idstr}`, true)
    xhr.send()
}


