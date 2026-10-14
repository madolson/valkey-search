import os
import pytest
from ft_info_parser import FTInfoParser
from indexes import *
from valkey.client import Valkey
from valkey_search_test_case import ValkeySearchTestCaseBase
from util import waiters
from valkeytestframework.conftest import resource_port_tracker


class TestExpired(ValkeySearchTestCaseBase):
    def test_expired_hashes_should_remove_from_index(self):
        """
        Test CMD flushall logic
        """
        index_name = "my_index"
        client: Valkey = self.server.get_new_client()
        hnsw_index = Index(
            index_name, [Vector("v", 3, type="HNSW", m=2, efc=1), Numeric("n")]
        )

        num_of_docs = 500
        hnsw_index.create(client)
        hnsw_index.load_data(client, num_of_docs)
        ft_info = hnsw_index.info(client)
        assert num_of_docs == ft_info.num_docs

        for i in range(0, num_of_docs):
            # expire half the keys
            if i % 2:
                client.pexpire(hnsw_index.keyname(i), 1)  # 1 ms

        waiters.wait_for_equal(
            lambda: hnsw_index.info(client).num_docs,
            num_of_docs / 2,
        )

    def test_hash_field_expiration_should_update_index(self):
        """
        Test that HEXPIRE on indexed fields triggers keyspace notification
        and updates the index correctly (Hash Field Expiration - HFE)
        """
        index_name = "my_index"
        key_name = "test_key"
        client: Valkey = self.server.get_new_client()
        index = Index(index_name, [Tag("t"), Numeric("n")])

        index.create(client)
        client.hset(key_name, mapping={"t": "mytag", "n": "1"})
        assert index.info(client).num_docs == 1

        result = client.execute_command("FT.SEARCH", index_name, "@t:{mytag}")
        assert result[0] == 1

        # Expire the indexed tag field using HEXPIRE
        client.execute_command("HEXPIRE", key_name, 1, "FIELDS", 1, "t")

        waiters.wait_for_equal(
            lambda: client.execute_command("FT.SEARCH", index_name, "@t:{mytag}")[0],
            0,
        )
        assert index.info(client).num_docs == 1

    @pytest.mark.parametrize("algo", ["HNSW", "FLAT"])
    @pytest.mark.parametrize("distance", ["L2", "COSINE"])
    def test_vector_expired_before_rdb_load(self, algo, distance):
        """
        A primary drops keys and hash fields whose TTL passed before the RDB
        is loaded, so the saved index can name vectors that no longer exist.
        """
        client: Valkey = self.server.get_new_client()
        index = Index(
            "my_index", [Vector("v", 3, type=algo, distance=distance), Numeric("n")]
        )
        index.create(client, wait_for_backfill=True)
        num_of_docs = 10
        index.load_data(client, num_of_docs)
        waiters.wait_for_equal(lambda: index.info(client).num_docs, num_of_docs)

        # Keep the expired key and field in memory so SAVE writes them.
        client.execute_command("DEBUG", "SET-ACTIVE-EXPIRE", "0")
        client.pexpire(index.keyname(0), 1)
        client.execute_command("HPEXPIRE", index.keyname(1), 1, "FIELDS", 1, "v")
        client.execute_command("SAVE")
        os.environ["SKIPLOGCLEAN"] = "1"
        self.server.restart(remove_rdb=False)
        client = self.server.get_new_client()
        index.wait_for_backfill_complete(client)

        def knn_keys():
            # Nearer to a zero vector than to any stored one under both
            # metrics, so a dropped record left live would take a slot.
            query = float_to_bytes([-1.0, -1.0, -1.0])
            return set(
                index.query(
                    client,
                    f"*=>[KNN {len(expected)} @v $q]",
                    "PARAMS", "2", "q", query, "DIALECT", "2",
                ).keys()
            )

        expected = {index.keyname(i).encode() for i in range(2, num_of_docs)}
        assert knn_keys() == expected
        assert index.info(client).num_docs == num_of_docs - 1

        index.write_data(client, num_of_docs, index.make_data(num_of_docs))
        expected.add(index.keyname(num_of_docs).encode())
        waiters.wait_for_equal(knn_keys, expected)
