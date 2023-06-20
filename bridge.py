import json
# import os
import sys
from pathlib import Path

import typesense.exceptions
import typesense.client

curr_dir = Path(__file__).absolute().parent
sys.path.insert(1, curr_dir.parent.as_posix())


class TypesenseBridgeException(Exception):
    pass


client = typesense.Client({
    'api_key': 'abcd',
    'nodes': [{
        'host': 'localhost',
        'port': '8108',
        'protocol': 'http'
    }],
    'connection_timeout_seconds': 2
})

# Drop pre-existing collection if any
try:
    client.collections['documents'].delete()
except typesense.exceptions.TypesenseClientError:
    pass

# Create a collection

create_response = client.collections.create({
    "name": "documents",
    "fields": [
        {"name": "filename", "type": "string"},
        {"name": "url", "type": "string"},
        {"name": "tags", "type": "string[]", "facet": True},
        {"name": "created_at", "type": "int32", "facet": True},
        {"name": "contents", "type": "string"}
    ],
    "default_sorting_field": "created_at"
})

print(create_response)

# Retrieve the collection we just created

retrieve_response = client.collections['documents'].retrieve()
print(retrieve_response)

# Try retrieving all collections
retrieve_all_response = client.collections.retrieve()
print(retrieve_all_response)

# Add a book

hunger_games_book = {
    'id': '1', 'original_publication_year': 2008, 'authors': ['Suzanne Collins'], 'average_rating': 4.34,
    'publication_year': 2008, 'title': 'The Hunger Games',
    'image_url': 'https://images.gr-assets.com/books/1447303603m/2767052.jpg',
    'ratings_count': 4780653
}

client.collections['books'].documents.create(hunger_games_book)

# Upsert the same document
print(client.collections['books'].documents.upsert(hunger_games_book))

# Or update it
hunger_games_book_updated = {'id': '1', 'average_rating': 4.45}
print(client.collections['books'].documents['1'].update(hunger_games_book_updated))

# Try updating with bad data (with coercion enabled)
hunger_games_book_updated = {'id': '1', 'average_rating': '4.55'}
print(client.collections['books'].documents['1'].update(hunger_games_book_updated, {'dirty_values': 'coerce_or_reject'}))

# Export the documents from a collection

export_output = client.collections['books'].documents.export()
print(export_output)

# Fetch a document in a collection

print(client.collections['books'].documents['1'].retrieve())

# Search for documents in a collection

print(client.collections['books'].documents.search({
    'q': 'hunger',
    'query_by': 'title',
    'sort_by': 'ratings_count:desc'
}))

# Make multiple search requests at the same time

print(client.multi_search.perform({'searches': [
    {
        'q': 'hunger',
        'query_by': 'title',
    },
    {
        'q': 'suzanne',
        'query_by': 'authors',
    }
]}, {'collection': 'books', 'sort_by': 'ratings_count:desc'}))

# Remove a document from a collection

print(client.collections['books'].documents['1'].delete())

# Import documents into a collection
docs_to_import = []
for exported_doc_str in export_output.split('\n'):
    docs_to_import.append(json.loads(exported_doc_str))

import_results = client.collections['books'].documents.import_(docs_to_import)
print(import_results)

# Upserting documents
import_results = client.collections['books'].documents.import_(docs_to_import, {
    'action': 'upsert',
})
print(import_results)

# Schema change: add optional field
schema_change = {"fields": [{"name": "in_stock", "optional": True, "type": "bool"}]}
print(client.collections['books'].update(schema_change))

# Drop the field
schema_change = {"fields": [{"name": "in_stock", "drop": True}]}
print(client.collections['books'].update(schema_change))

# Deleting documents matching a filter query
print(client.collections['books'].documents.delete({'filter_by': 'ratings_count: 4780653'}))

# Drop the collection
drop_response = client.collections['books'].delete()
print(drop_response)
