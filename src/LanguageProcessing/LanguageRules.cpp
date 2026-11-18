/**
 * @file LanguageRules.cpp
 * @brief Implementation of language-specific rules
 *
 */

#include "LanguageProcessing/LanguageRules.h"

namespace {
    // Words in these lists carry accents ("qué", "où", "más"), and Qt's \b and
    // \w are ASCII-only unless asked: "qué quieres comer" never matched its
    // question word (Session 579 review, found in review).
    constexpr QRegularExpression::PatternOptions kWordRegexOptions =
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption;

    // Build a safe alternation group by escaping each token
    static QString alternationFor(const QStringList& tokens)
    {
        QStringList escaped;
        escaped.reserve(tokens.size());
        for (const auto& t : tokens) {
            escaped << QRegularExpression::escape(t);
        }
        return escaped.join('|');
    }
}

LanguageRulesManager& LanguageRulesManager::instance()
{
    static LanguageRulesManager instance;
    return instance;
}

LanguageRulesManager::LanguageRulesManager()
{
    initializeRules();
}

void LanguageRulesManager::initializeRules()
{
    initializeEnglishRules();
    initializeSpanishRules();
    initializeGermanRules();
    initializeItalianRules();
    initializePortugueseRules();
    initializeFrenchRules();
    initializeDutchRules();
    initializeSwedishRules();
    initializeNorwegianRules();
    initializeDanishRules();
    initializePolishRules();
    initializeJapaneseRules();
    initializeKoreanRules();

    // Set English as fallback
    m_fallbackRules = m_rules["en"];
}

void LanguageRulesManager::initializeEnglishRules()
{
    LanguageRules rules;

    // Question words
    rules.questionWords = {
        "what", "where", "when", "why", "who", "how",
        "is", "are", "can", "could", "would", "should",
        "do", "does", "did", "will", "shall"
    };

    // Discourse markers
    // Words after which a comma is put when they open a slice. Not a word that
    // also opens an ordinary sentence: "well" ("Well done"), "no" ("No one
    // came"), "however" ("However much you try"), and in the other languages "bien" ("Bien que", "Bien hecho"),
    // "no"/"não"/"non" ("No sé", "Não sei", "Non so"), "bem" ("Bem vindo")
    // (Session 579 review, found in review). A missing comma there costs less
    // than a wrong one.
    rules.discourseMarkers = {
        "okay", "ok", "yes", "moreover",
        "furthermore", "therefore", "meanwhile", "nevertheless",
        "in fact", "in addition", "for example", "for instance",
        "as a result", "by the way", "in general", "in summary",
        "in conclusion", "actually", "basically", "honestly",
        "frankly", "indeed", "naturally", "obviously", "apparently"
    };

    // Connectors
    // Words a comma is put in front of when none is there, in every language
    // below (Session 579 review, found in review). Only words that, in the
    // middle of a sentence, introduce a clause that takes a comma. What must
    // never be here:
    //   - "and"/"or" and their equivalents: joining two words they take no
    //     comma ("apples and pears", "Brot und Butter");
    //   - a word that is also an adverb or a preposition: "so" ("I think so"),
    //     "yet" ("not yet"), "while" ("a while"), "since" ("since Monday"),
    //     German "also"/"seit"/"während", Italian "da" ("from"), French
    //     "pendant"/"depuis", Spanish/Portuguese "desde", Scandinavian "så"/"da"/
    //     "selv", and the like;
    //   - an adverb that moves around inside a clause: "however", "jedoch",
    //     "sin embargo", "cependant", "tuttavia", "no entanto", "echter",
    //     "dock", "dog", "jednak" ("Er hat jedoch nichts gesagt");
    //   - "because" and its equivalents where the language writes no comma
    //     before them (English, Spanish, Italian, Portuguese, French, Dutch,
    //     Swedish, Norwegian, Danish). German and Polish write one, so "weil",
    //     "wenn", "ponieważ", "bo", "gdy", "kiedy" stay.
    rules.connectors = {
        "but", "although"
    };

    // Trailing connectors (incomplete sentence markers)
    rules.trailingConnectors = {
        "and", "or", "but", "so", "because", "if", "when",
        "then", "to", "of", "for", "with", "that", "which",
        "who", "while", "as", "since", "though", "although"
    };

    // Common homophones
    rules.homophones = {
        {"their", {"there", "they're"}},
        {"your", {"you're"}},
        {"its", {"it's"}},
        {"to", {"too", "two"}},
        {"hear", {"here"}},
        {"buy", {"by", "bye"}}
    };

    m_rules["en"] = rules;
}

void LanguageRulesManager::initializeSpanishRules()
{
    LanguageRules rules;

    // Question words
    rules.questionWords = {
        "qué", "dónde", "cuándo", "por qué", "quién", "cómo"
    };

    // Discourse markers
    rules.discourseMarkers = {
        "bueno", "vale", "sí", "sin embargo",
        "además", "por lo tanto", "mientras tanto", "de hecho",
        "por ejemplo", "como resultado", "en general", "en resumen",
        "en conclusión", "realmente", "básicamente", "francamente",
        "obviamente", "aparentemente", "naturalmente"
    };

    // Connectors
    rules.connectors = {
        "pero", "aunque", "así que"
    };

    // Trailing connectors
    rules.trailingConnectors = {
        "y", "o", "pero", "así que", "porque", "si", "cuando",
        "para", "de", "con", "que", "quien", "mientras", "como",
        "desde", "aunque"
    };

    // Common homophones
    rules.homophones = {
        {"haya", {"halla", "aya"}},
        {"hay", {"ahí", "ay"}},
        {"valla", {"vaya", "baya"}},
        {"bello", {"vello"}},
        {"tuvo", {"tubo"}}
    };

    m_rules["es"] = rules;
}

void LanguageRulesManager::initializeGermanRules()
{
    LanguageRules rules;

    // Question words
    rules.questionWords = {
        "was", "wo", "wann", "warum", "wer", "wie",
        "ist", "sind", "kann", "könnte", "würde", "sollte",
        "macht", "tut", "wird"
    };

    // Discourse markers
    // German puts the verb straight after an opening adverb, with no comma
    // ("Trotzdem gehe ich", "Zum Beispiel kann man"), so only the answer words
    // take one (Session 579 review, found in review). The same holds for Dutch,
    // Swedish, Norwegian and Danish below. Never add an adverb to these lists.
    rules.discourseMarkers = {
        "okay", "ja", "nein"
    };

    // Connectors
    rules.connectors = {
        "aber", "obwohl", "weil", "wenn"
    };

    // Trailing connectors
    rules.trailingConnectors = {
        "und", "oder", "aber", "also", "weil", "wenn", "als",
        "zu", "von", "für", "mit", "dass", "der", "die", "das",
        "welcher", "während", "seit", "obwohl"
    };

    // Common homophones
    rules.homophones = {
        {"das", {"dass"}},
        {"wider", {"wieder"}},
        {"seit", {"seid"}},
        {"war", {"wahr"}},
        {"lehren", {"leeren"}}
    };

    m_rules["de"] = rules;
}

void LanguageRulesManager::initializeItalianRules()
{
    LanguageRules rules;

    // Question words
    // Only question words. Italian, like Spanish and Portuguese, asks a yes/no
    // question with the word order of a statement ("Sono stanco" / "Sono
    // stanco?"), so a verb at the start cannot mark a question and "Sono stanco
    // quindi vado a dormire" became a question (Session 579 review, found in
    // review). Never add a verb form here, nor to the Spanish or Portuguese list.
    rules.questionWords = {
        "cosa", "dove", "quando", "perché", "chi", "come"
    };

    // Discourse markers
    rules.discourseMarkers = {
        "bene", "okay", "sì", "tuttavia", "inoltre",
        "pertanto", "nel frattempo", "comunque", "infatti",
        "per esempio", "di conseguenza", "in generale",
        "in sintesi", "in conclusione", "effettivamente",
        "fondamentalmente", "francamente", "ovviamente",
        "apparentemente", "naturalmente"
    };

    // Connectors
    rules.connectors = {
        "ma", "sebbene"
    };

    // Trailing connectors
    rules.trailingConnectors = {
        "e", "o", "ma", "quindi", "perché", "se", "quando",
        "per", "di", "con", "che", "chi", "mentre", "come",
        "da", "sebbene"
    };

    // Common homophones
    rules.homophones = {
        {"ce", {"c'è"}},
        {"hanno", {"anno"}},
        {"ha", {"a"}},
        {"ho", {"o"}},
        {"la", {"l'a"}}
    };

    m_rules["it"] = rules;
}

void LanguageRulesManager::initializePortugueseRules()
{
    LanguageRules rules;

    // Question words
    rules.questionWords = {
        "o que", "onde", "quando", "por que", "quem", "como"
    };

    // Discourse markers
    rules.discourseMarkers = {
        "ok", "sim", "no entanto", "além disso",
        "portanto", "enquanto isso", "entretanto", "de fato",
        "por exemplo", "como resultado", "em geral", "em resumo",
        "em conclusão", "realmente", "basicamente", "francamente",
        "obviamente", "aparentemente", "naturalmente"
    };

    // Connectors
    rules.connectors = {
        "mas", "embora"
    };

    // Trailing connectors
    rules.trailingConnectors = {
        "e", "ou", "mas", "então", "porque", "se", "quando",
        "para", "de", "com", "que", "quem", "enquanto", "como",
        "desde", "embora"
    };

    // Common homophones
    rules.homophones = {
        {"mas", {"mais"}},
        {"há", {"a"}},
        {"vêm", {"vem"}},
        {"pôr", {"por"}},
        {"concerto", {"conserto"}}
    };

    m_rules["pt"] = rules;
}

void LanguageRulesManager::initializeFrenchRules()
{
    LanguageRules rules;

    // Question words
    rules.questionWords = {
        "quoi", "où", "quand", "pourquoi", "qui", "comment",
        "est", "sont", "peut", "pourrait", "devrait", "fait",
        "fera", "a fait"
    };

    // Discourse markers
    rules.discourseMarkers = {
        "d'accord", "oui", "cependant", "de plus",
        "donc", "pendant ce temps", "néanmoins", "en fait",
        "par exemple", "en conséquence", "en général", "en résumé",
        "en conclusion", "effectivement", "fondamentalement",
        "franchement", "évidemment", "apparemment", "naturellement"
    };

    // Connectors
    rules.connectors = {
        "mais", "bien que"
    };

    // Trailing connectors
    rules.trailingConnectors = {
        "et", "ou", "mais", "donc", "parce que", "si", "quand",
        "pour", "de", "avec", "que", "qui", "pendant", "comme",
        "depuis", "bien que"
    };

    // Common homophones
    rules.homophones = {
        {"a", {"à"}},
        {"ou", {"où"}},
        {"ce", {"se"}},
        {"son", {"sont"}},
        {"ces", {"ses", "c'est"}}
    };

    m_rules["fr"] = rules;
}

void LanguageRulesManager::initializeDutchRules()
{
    LanguageRules rules;

    // Question words
    rules.questionWords = {
        "wat", "waar", "wanneer", "waarom", "wie", "hoe",
        "is", "zijn", "kan", "kon", "zou", "moet", "moeten",
        "doet", "doen", "deed", "zal", "zullen"
    };

    // Discourse markers
    rules.discourseMarkers = {
        "oké", "ok", "ja", "nee"
    };

    // Connectors
    rules.connectors = {
        "maar", "hoewel", "alhoewel"
    };

    // Trailing connectors
    rules.trailingConnectors = {
        "en", "of", "maar", "dus", "omdat", "als", "wanneer",
        "dan", "om", "van", "voor", "met", "dat", "die",
        "wie", "terwijl", "zoals", "sinds", "hoewel"
    };

    // Common homophones
    rules.homophones = {
        {"hun", {"hen"}},
        {"dan", {"als"}},
        {"word", {"wordt"}},
        {"als", {"dan"}},
        {"jou", {"jouw"}},
        {"der", {"daar"}}
    };

    m_rules["nl"] = rules;
}

void LanguageRulesManager::initializeSwedishRules()
{
    LanguageRules rules;

    // Question words
    rules.questionWords = {
        "vad", "var", "vart", "när", "varför", "vem", "vilken", "hur",
        "är", "kan", "kunde", "skulle", "bör", "borde", "måste",
        "gör", "gjorde", "ska", "kommer"
    };

    // Discourse markers
    rules.discourseMarkers = {
        "ja", "nej", "okej", "ok"
    };

    // Connectors
    rules.connectors = {
        "men", "fastän"
    };

    // Trailing connectors
    rules.trailingConnectors = {
        "och", "eller", "men", "så", "eftersom", "om", "när",
        "då", "att", "av", "för", "med", "som", "vilken",
        "vem", "medan", "sedan", "fastän"
    };

    // Common homophones
    rules.homophones = {
        {"de", {"dem", "dom"}},
        {"än", {"en"}},
        {"sin", {"sitt"}},
        {"är", {"ä"}},
        {"där", {"dä"}},
        {"sen", {"sedan"}}
    };

    m_rules["sv"] = rules;
}

void LanguageRulesManager::initializeNorwegianRules()
{
    LanguageRules rules;

    // Question words (Bokmål)
    rules.questionWords = {
        "hva", "hvor", "når", "hvorfor", "hvem", "hvilken", "hvordan",
        "er", "kan", "kunne", "skulle", "bør", "burde", "må",
        "gjør", "gjorde", "skal", "kommer", "vil"
    };

    // Discourse markers
    rules.discourseMarkers = {
        "ja", "nei", "ok", "okei"
    };

    // Connectors
    rules.connectors = {
        "men", "selv om"
    };

    // Trailing connectors
    rules.trailingConnectors = {
        "og", "eller", "men", "så", "fordi", "om", "når",
        "da", "å", "av", "for", "med", "som", "hvilken",
        "hvem", "mens", "siden", "selv om"
    };

    // Common homophones
    rules.homophones = {
        {"de", {"dem"}},
        {"en", {"et"}},
        {"sin", {"sitt"}},
        {"å", {"og"}},
        {"da", {"når"}},
        {"sei", {"si"}}
    };

    m_rules["no"] = rules;
}

void LanguageRulesManager::initializeDanishRules()
{
    LanguageRules rules;

    // Question words (Danish)
    rules.questionWords = {
        "hvad", "hvor", "hvornår", "hvorfor", "hvem", "hvilken", "hvordan",
        "er", "kan", "kunne", "skulle", "bør", "burde", "må",
        "gør", "gjorde", "skal", "kommer", "vil"
    };

    // Discourse markers
    rules.discourseMarkers = {
        "ja", "nej", "okay", "ok"
    };

    // Connectors
    rules.connectors = {
        "men", "selvom"
    };

    // Trailing connectors
    rules.trailingConnectors = {
        "og", "eller", "men", "så", "fordi", "om", "når",
        "da", "at", "af", "for", "med", "som", "hvilken",
        "hvem", "mens", "siden", "selvom"
    };

    // Common homophones
    rules.homophones = {
        {"de", {"dem"}},
        {"en", {"et"}},
        {"sin", {"sit"}},
        {"å", {"og"}},
        {"da", {"når"}},
        {"i", {"vi"}},
        {"hvor", {"hvordan"}}
    };

    m_rules["da"] = rules;
}

void LanguageRulesManager::initializePolishRules()
{
    LanguageRules rules;

    // Question words (Polish)
    rules.questionWords = {
        "co", "gdzie", "kiedy", "dlaczego", "kto", "jak", "który", "jaki",
        "czy", "ile", "czyj", "czym", "dokąd", "skąd",
        "jest", "są", "może", "mógł", "mogę", "powinien",
        "robi", "robił", "będzie", "był"
    };

    // Discourse markers
    rules.discourseMarkers = {
        "tak", "nie", "dobrze", "ok", "jednak", "ponadto", "dlatego",
        "mimo to", "natomiast", "faktycznie", "w rzeczywistości",
        "na przykład", "w rezultacie", "ogólnie", "ogólnie mówiąc",
        "podsumowując", "na koniec", "właściwie", "w zasadzie",
        "szczerze mówiąc", "oczywiście", "naturalnie", "bez wątpienia"
    };

    // Connectors
    rules.connectors = {
        "ale", "więc",
        "chociaż", "choć", "ponieważ", "bo", "gdy", "kiedy"
    };

    // Trailing connectors
    rules.trailingConnectors = {
        "i", "lub", "ale", "więc", "bo", "ponieważ", "jeśli",
        "gdy", "kiedy", "że", "aby", "dla", "z", "który",
        "kto", "jak", "po", "przed", "przez", "chociaż"
    };

    // Common homophones
    rules.homophones = {
        {"u", {"w"}},
        {"rz", {"ż"}},
        {"ó", {"u"}},
        {"ch", {"h"}},
        {"jest", {"jeść"}},
        {"może", {"morze"}},
        {"na", {"no"}},
        {"się", {"sie"}}
    };

    m_rules["pl"] = rules;
}

void LanguageRulesManager::initializeJapaneseRules()
{
    LanguageRules rules;

    // Question words - Japanese (Hiragana/Kanji)
    rules.questionWords = {
        "何", "なに", "なん",
        "どこ", "何処",
        "いつ", "何時",
        "なぜ", "何故",
        "誰", "だれ",
        "どう", "如何",
        "どの", "どれ", "どちら",
        "幾つ", "いくつ"
    };

    // Discourse markers
    rules.discourseMarkers = {
        "はい", "ええ", "うん",
        "いいえ", "いえ", "ううん",
        "でも", "しかし", "けれども",
        "そして", "また",
        "だから", "それで",
        "ところで", "さて",
        "つまり", "要するに",
        "例えば", "たとえば",
        "実は", "じつは",
        "やはり", "やっぱり"
    };

    // Connectors
    rules.connectors = {
        "が", "けど", "のに",
        "から", "ので",
        "て",
        "なら", "ば"
    };

    // Trailing connectors
    rules.trailingConnectors = {
        "が", "けど", "けれど", "けれども",
        "から", "ので", "し", "て",
        "たり", "と", "なら", "ば", "の", "ん"
    };

    // Common homophones
    rules.homophones = {
        {"橋", {"箸", "端"}},
        {"雨", {"飴"}},
        {"神", {"紙", "髪"}},
        {"市", {"死"}},
        {"飼う", {"買う"}},
        {"聞く", {"効く", "利く"}},
        {"会う", {"合う"}},
        {"書く", {"描く"}},
        {"立つ", {"建つ"}},
        {"見る", {"観る", "診る"}}
    };

    m_rules["ja"] = rules;
}

void LanguageRulesManager::initializeKoreanRules()
{
    LanguageRules rules;

    // Question words - Korean (Hangul)
    rules.questionWords = {
        "무엇", "뭐",
        "어디",
        "언제",
        "왜",
        "누구", "누가",
        "어떻게",
        "어느", "무슨",
        "몇"
    };

    // Discourse markers
    rules.discourseMarkers = {
        "네", "예",
        "아니요", "아니",
        "그러나", "하지만",
        "그리고",
        "그래서", "따라서",
        "그런데",
        "즉",
        "예를들어", "예를 들어",
        "사실",
        "역시"
    };

    // Connectors
    rules.connectors = {
        "그러나", "하지만", "지만",
        "그래서", "아서", "어서",
        "면",
        "는데", "ㄴ데"
    };

    // Trailing connectors
    rules.trailingConnectors = {
        "그리고", "하고", "지만", "는데",
        "아서", "어서", "면", "거나", "고"
    };

    // Common homophones
    rules.homophones = {
        {"배", {"倍", "杯"}},
        {"사", {"四", "死", "士"}},
        {"이", {"二", "耳"}},
        {"일", {"一", "日", "仕事"}},
        {"말", {"馬", "語"}},
        {"자", {"者", "字"}},
        {"가다", {"去"}},
        {"오다", {"來"}},
        {"보다", {"見"}},
        {"먹다", {"食"}}
    };

    m_rules["ko"] = rules;
}

const LanguageRules& LanguageRulesManager::getRules(const QString& language) const
{
    // Return a stable reference; do NOT use m_rules[language] here (would be a temporary in const context)
    const auto it = m_rules.constFind(language);
    if (it != m_rules.cend()) {
        return it.value();
    }
    return m_fallbackRules;
}

QStringList LanguageRulesManager::getSupportedLanguages() const
{
    return m_rules.keys();
}

QRegularExpression LanguageRulesManager::createQuestionRegex(const QString& language) const
{
    const LanguageRules& rules = getRules(language);
    const QString pattern = QString("^(%1)\\b").arg(alternationFor(rules.questionWords));
    return QRegularExpression(pattern, kWordRegexOptions);
}

QRegularExpression LanguageRulesManager::createDiscourseMarkerRegex(const QString& language) const
{
    const LanguageRules& rules = getRules(language);
    const QString pattern = QString("^\\s*((?:%1))\\b\\s+(?!,)").arg(alternationFor(rules.discourseMarkers));
    return QRegularExpression(pattern, kWordRegexOptions);
}

QRegularExpression LanguageRulesManager::createConnectorRegex(const QString& language) const
{
    const LanguageRules& rules = getRules(language);
    // The connector must introduce more words: one at the very end of a slice
    // ("I disagree, however") is left alone.
    const QString pattern = QString("(?<![,;])\\s+(%1)\\b(?=\\s+\\S)").arg(alternationFor(rules.connectors));
    return QRegularExpression(pattern, kWordRegexOptions);
}

QRegularExpression LanguageRulesManager::createTrailingConnectorRegex(const QString& language) const
{
    const LanguageRules& rules = getRules(language);
    const QString pattern = QString("\\b(%1)\\s*$").arg(alternationFor(rules.trailingConnectors));
    return QRegularExpression(pattern, kWordRegexOptions);
}
