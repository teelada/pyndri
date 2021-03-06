import operator
import os
import shutil
import subprocess
import tempfile
import unittest

import pyndri


class KrovetzStemmingTest(unittest.TestCase):

    def test_stemming(self):
        self.assertEqual(pyndri.stem('predictions'), 'prediction')
        self.assertEqual(pyndri.stem('marketing'), 'marketing')
        self.assertEqual(pyndri.stem('strategies'), 'strategy')


class IndriTest(unittest.TestCase):

    CORPUS = """<DOC>
<DOCNO>lorem</DOCNO>
<TEXT>
Lorem ipsum dolor sit amet, consectetur adipiscing elit. Duis in magna id urna lobortis tristique sed eget sem. Fusce fringilla nibh in tortor venenatis, eget laoreet metus luctus. Maecenas velit arcu, ullamcorper quis mauris ut, posuere consectetur nibh. Integer sodales mi consectetur arcu gravida porta. Cras maximus sapien non nisi cursus, sit amet sollicitudin tortor porttitor. Nulla scelerisque eu est at fringilla. Cras felis elit, cursus in efficitur a, varius id nisl. Morbi lorem nulla, ornare vitae porta eget, convallis vestibulum nulla. Integer vestibulum et sem ac scelerisque.
</TEXT>
</DOC>
<DOC>
<DOCNO>hamlet</DOCNO>
<TEXT>
ACT I  SCENE I. Elsinore. A platform before the castle.  FRANCISCO at his post. Enter to him BERNARDO BERNARDO Who's there? FRANCISCO Nay, answer me: stand, and unfold yourself. BERNARDO Long live the king! FRANCISCO Bernardo? BERNARDO He. FRANCISCO You come most carefully upon your hour. BERNARDO 'Tis now struck twelve; get thee to bed, Francisco. FRANCISCO For this relief much thanks: 'tis bitter cold, And I am sick at heart.
</TEXT>
</DOC>
<DOC>
<DOCNO>romeo</DOCNO>
<TEXT>
ACT I  PROLOGUE  Two households, both alike in dignity, In fair Verona, where we lay our scene, From ancient grudge break to new mutiny, Where civil blood makes civil hands unclean. From forth the fatal loins of these two foes A pair of star-cross'd lovers take their life; Whose misadventured piteous overthrows Do with their death bury their parents' strife. The fearful passage of their death-mark'd love, And the continuance of their parents' rage, Which, but their children's end, nought could remove, Is now the two hours' traffic of our stage; The which if you with patient ears attend, What here shall miss, our toil shall strive to mend. SCENE I. Verona. A public place.  Enter SAMPSON and GREGORY, of the house of Capulet, armed with swords and bucklers SAMPSON Gregory, o' my word, we'll not carry coals. GREGORY No, for then we should be colliers. SAMPSON I mean, an we be in choler, we'll draw. GREGORY Ay, while you live, draw your neck out o' the collar. SAMPSON I strike quickly, being moved. GREGORY But thou art not quickly moved to strike. SAMPSON A dog of the house of Montague moves me. GREGORY To move is to stir; and to be valiant is to stand: therefore, if thou art moved, thou runn'st away. SAMPSON A dog of that house shall move me to stand: I will take the wall of any man or maid of Montague's. GREGORY That shows thee a weak slave; for the weakest goes to the wall. SAMPSON True; and therefore women, being the weaker vessels, are ever thrust to the wall: therefore I will push Montague's men from the wall, and thrust his maids to the wall. GREGORY The quarrel is between our masters and us their men. SAMPSON 'Tis all one, I will show myself a tyrant: when I have fought with the men, I will be cruel with the maids, and cut off their heads. GREGORY The heads of the maids? SAMPSON Ay, the heads of the maids, or their maidenheads; take it in what sense thou wilt. GREGORY They must take it in sense that feel it. SAMPSON Me they shall feel while I am able to stand: and 'tis known I am a pretty piece of flesh. GREGORY 'Tis well thou art not fish; if thou hadst, thou hadst been poor John. Draw thy tool! here comes two of the house of the Montagues. SAMPSON My naked weapon is out: quarrel, I will back thee. GREGORY How! turn thy back and run? SAMPSON Fear me not. GREGORY No, marry; I fear thee! SAMPSON Let us take the law of our sides; let them begin. GREGORY I will frown as I pass by, and let them take it as they list. SAMPSON Nay, as they dare. I will bite my thumb at them; which is a disgrace to them, if they bear it. Enter ABRAHAM and BALTHASAR  ABRAHAM Do you bite your thumb at us, sir? SAMPSON I do bite my thumb, sir. ABRAHAM Do you bite your thumb at us, sir? SAMPSON [Aside to GREGORY] Is the law of our side, if I say ay? GREGORY No. SAMPSON No, sir, I do not bite my thumb at you, sir, but I bite my thumb, sir. GREGORY Do you quarrel, sir? ABRAHAM Quarrel sir! no, sir. SAMPSON If you do, sir, I am for you: I serve as good a man as you.
</TEXT>
</DOC>
"""

    INDRI_CONFIG = """<parameters>
<index>index/</index>
<memory>1024M</memory>
<storeDocs>true</storeDocs>
<corpus><path>corpus.trectext</path><class>trectext</class></corpus>
<stemmer><name>krovetz</name></stemmer>
</parameters>"""

    def setUp(self):
        self.test_dir = tempfile.mkdtemp()

        with open(os.path.join(self.test_dir,
                               'corpus.trectext'),
                  'w', encoding='latin1') as f:
            f.write(self.CORPUS)

        with open(os.path.join(self.test_dir,
                               'IndriBuildIndex.conf'), 'w') as f:
            f.write(self.INDRI_CONFIG)

        with open(os.devnull, "w") as f:
            ret = subprocess.call(['IndriBuildIndex', 'IndriBuildIndex.conf'],
                                  stdout=f,
                                  cwd=self.test_dir)

        self.assertEqual(ret, 0)

        self.index_path = os.path.join(self.test_dir, 'index')
        self.assertTrue(os.path.exists(self.index_path))

        self.index = pyndri.Index(self.index_path)

    def test_meta(self):
        self.assertEqual(self.index.document_base(), 1)
        self.assertEqual(self.index.maximum_document(), 4)

    def test_simple_query(self):
        self.assertEqual(
            self.index.query('ipsum'),
            ((1, -6.373564749941117),))

        self.assertEqual(
            self.index.query('his'),
            ((2, -5.794010932279138),
             (3, -5.972370287143733)))

    def test_query_documentset(self):
        self.assertEqual(
            self.index.query(
                'his',
                document_set=map(
                    operator.itemgetter(1),
                    self.index.document_ids(['hamlet']))),
            ((2, -5.794010932279138),))

    def test_query_results_requested(self):
        self.assertEqual(
            self.index.query(
                'his',
                results_requested=1),
            ((2, -5.794010932279138),))

    def test_query_snippets(self):
        self.assertEqual(
            self.index.query('ipsum', include_snippets=True),
            ((1, -6.373564749941117,
              'Lorem IPSUM dolor sit amet, consectetur '
              'adipiscing\nelit. Duis...'),))

    def test_document_length(self):
        self.assertEqual(self.index.document_length(1), 88)
        self.assertEqual(self.index.document_length(2), 71)
        self.assertEqual(self.index.document_length(3), 573)

    def test_raw_dictionary(self):
        token2id, id2token, id2df = self.index.get_dictionary()
        id2tf = self.index.get_term_frequencies()

        self.assertEqual(len(token2id), len(id2token))

        for token, idx in token2id.items():
            self.assertEqual(id2token[idx], token)

            self.assertGreaterEqual(id2df[idx], 1)
            self.assertGreaterEqual(id2tf[idx], 1)

    def test_document(self):
        token2id, id2token, id2df = self.index.get_dictionary()

        first_id, first_tokens = self.index.document(1)

        self.assertEqual(first_id, 'lorem')

        self.assertEqual(len(first_tokens), 88)

        self.assertEqual(
            [id2token[token_id] for token_id in first_tokens],
            ['lorem', 'ipsum', 'dolor', 'sit', 'amet', 'consectetur',
             'adipisc', 'elit', 'dui', 'in', 'magna', 'id', 'urna',
             'loborti', 'tristique', 'sed', 'eget', 'sem', 'fusce',
             'fringilla', 'nibh', 'in', 'tort', 'venenati', 'eget',
             'laoreet', 'metu', 'luctu', 'maecena', 'velit', 'arcu',
             'ullamcorper', 'qui', 'mauri', 'ut', 'posuere', 'consectetur',
             'nibh', 'integer', 'sodale', 'mi', 'consectetur', 'arcu',
             'gravida', 'porta', 'cra', 'maximu', 'sapien', 'non', 'nisi',
             'cursu', 'sit', 'amet', 'sollicitudin', 'tort', 'porttitor',
             'nulla', 'scelerisque', 'eu', 'est', 'at', 'fringilla', 'cra',
             'feli', 'elit', 'cursu', 'in', 'efficitur', 'a', 'variu', 'id',
             'nisl', 'morbi', 'lorem', 'nulla', 'ornare', 'vitae', 'porta',
             'eget', 'convalli', 'vestibulum', 'nulla', 'integer',
             'vestibulum', 'et', 'sem', 'ac', 'scelerisque'])

    def test_iter_index(self):
        ext_doc_ids = [
            self.index.document(int_doc_id)[0]
            for int_doc_id in range(
                self.index.document_base(),
                self.index.maximum_document())]

        self.assertEqual(ext_doc_ids,
                         ['lorem', 'hamlet', 'romeo'])

    def tearDown(self):
        shutil.rmtree(self.test_dir)
        del self.index

if __name__ == '__main__':
    unittest.main()
