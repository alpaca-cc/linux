#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>

#include <sound/dmaengine_pcm.h>

#define WB_I2S_RATES \
	(SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_44100 | \
	 SNDRV_PCM_RATE_48000)

struct wb_i2s {
	void __iomem *regs;

	void __iomem *phy_base;

	struct snd_pcm_hw_constraint_ratnums rate_constraints;
};

/* PCM */
#define WB_DMABUF_SIZE	(64 * 1024)

static int wb_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = WB_DMABUF_SIZE;

	printk("SJK DEBUG: %s\n", __func__);

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;
	buf->area = dma_alloc_coherent(pcm->card->dev, size,
			&buf->addr, GFP_KERNEL);
	printk("wb-pcm: alloc dma buffer: area=%p, addr=%p, size=%zu\n",
	       (void *)buf->area, (void *)(long)buf->addr, size);

	if (!buf->area)
		return -ENOMEM;

	buf->bytes = size;
	return 0;
}

int wb_pcm_mmap(struct snd_pcm_substream *substream,
	struct vm_area_struct *vma)
{
	printk("SJK DEBUG: %s\n", __func__);

	return remap_pfn_range(vma, vma->vm_start,
		       substream->dma_buffer.addr >> PAGE_SHIFT,
		       vma->vm_end - vma->vm_start, vma->vm_page_prot);
}
EXPORT_SYMBOL_GPL(wb_pcm_mmap);

int wb_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int ret;

	printk("SJK DEBUG: %s\n", __func__);

	ret = dma_coerce_mask_and_coherent(card->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		pr_debug("wb-pcm: allocating PCM playback DMA buffer\n");
		ret = wb_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		pr_debug("wb-pcm: allocating PCM capture DMA buffer\n");
		ret = wb_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}
 out:
	return ret;
}
EXPORT_SYMBOL_GPL(wb_pcm_new);

void wb_pcm_free(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;
	int stream;

	printk("SJK DEBUG: %s\n", __func__);

	for (stream = 0; stream < 2; stream++) {
		substream = pcm->streams[stream].substream;
		if (!substream)
			continue;

		buf = &substream->dma_buffer;
		if (!buf->area)
			continue;
		dma_free_coherent(pcm->card->dev, buf->bytes,
				  buf->area, buf->addr);
		buf->area = NULL;
	}
}
EXPORT_SYMBOL_GPL(wb_pcm_free);

/* DMA */
static const struct snd_pcm_hardware wb_pcm_dma_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_RESUME |
				  SNDRV_PCM_INFO_PAUSE,
	.period_bytes_min	= 256,		/* lighting DMA overhead */
	.period_bytes_max	= 2 * 0xffff,	/* if 2 bytes format */
	.periods_min		= 8,
	.periods_max		= 1024,		/* no limit */
	.buffer_bytes_max	= WB_DMABUF_SIZE,
};

static void wb_pcm_dma_irq(u32 ssc_sr, struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct wb_pcm_dma_params *prtd;

	prtd = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	printk("SJK DEBUG: %s\n", __func__);
	//TODO
}

static int wb_pcm_configure_dma(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct dma_slave_config *slave_config)
{
	int ret;

	printk("SJK DEBUG: %s\n", __func__);

	ret = snd_hwparams_to_dma_slave_config(substream, params, slave_config);
	if (ret) {
		pr_err("wb-pcm: hwparams to dma slave configure failed\n");
		return ret;
	}
	return 0;
}

static const struct snd_dmaengine_pcm_config wb_dmaengine_pcm_config = {
	.prepare_slave_config = wb_pcm_configure_dma,
	.pcm_hardware = &wb_pcm_dma_hardware,
	.prealloc_buffer_size = WB_DMABUF_SIZE,
};

int wb_pcm_dma_platform_register(struct device *dev)
{
	printk("SJK DEBUG: %s\n", __func__);
	return devm_snd_dmaengine_pcm_register(dev, &wb_dmaengine_pcm_config,
					       0 /*SND_DMAENGINE_PCM_FLAG_NO_RESIDUE*/);
}
EXPORT_SYMBOL(wb_pcm_dma_platform_register);

void wb_pcm_dma_platform_unregister(struct device *dev)
{
	printk("SJK DEBUG: %s\n", __func__);
	snd_dmaengine_pcm_unregister(dev);
}
EXPORT_SYMBOL(wb_pcm_dma_platform_unregister);

/* DAI */
#define WB_I2S_REG_PRESCALER	0x00

static void wb_i2s_write_reg(struct wb_i2s *i2s, loff_t offset, u32 value)
{
	iowrite32be(value, i2s->regs + offset);
}

static u32 wb_i2s_read_reg(struct wb_i2s *i2s, loff_t offset)
{
	return ioread32be(i2s->regs + offset);
}

static int wb_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
	struct snd_soc_dai *dai)
{
	struct wb_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	unsigned int mask, val;

#if 0
	printk("SJK DEBUG: %s\n", __func__);
#endif

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		mask = 1; //TODO
	else
		mask = 0; //TODO

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		//TODO
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		//TODO
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int wb_i2s_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct wb_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	unsigned int prescaler;

	switch (params_rate(params)) {
	case 48000:
		prescaler = 32;
		break;
	case 11025:
		prescaler = 128;
		break;
	case 22050:
		prescaler = 64;
		break;
	case 44100:
		prescaler = 32;
		break;
	default:
		printk("SJK DEBUG: invalid rate\n");
		return -EINVAL;
	}

	printk("SJK DEBUG: %s: prescaler = %d\n", __func__, prescaler);

	wb_i2s_write_reg(i2s, WB_I2S_REG_PRESCALER, prescaler);

	return 0;
}

static int wb_i2s_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct wb_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	uint32_t mask;
	int ret;

	printk("SJK DEBUG: %s\n", __func__);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		mask = 0;//WB_I2S_RESET_RX_FIFO;
	else
		mask = 1;//WB_I2S_RESET_TX_FIFO;

	//regmap_write(i2s->regmap, WB_I2S_REG_RESET, mask);
/*
	ret = snd_pcm_hw_constraint_ratnums(substream->runtime, 0,
			   SNDRV_PCM_HW_PARAM_RATE,
			   &i2s->rate_constraints);
	if (ret) {
		printk("SJK DEBUG: %s: snd_pcm_hw_constraint_ratnums failed: %d\n",
		       __func__, ret);
		return ret;
	}
*/

	return 0;
}

static void wb_i2s_shutdown(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct wb_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	printk("SJK DEBUG: %s\n", __func__);
}
/*
static int wb_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct wb_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	printk("SJK DEBUG: %s\n", __func__);

	return 0;
}
*/
static const struct snd_soc_dai_ops wb_i2s_dai_ops = {
	.startup = wb_i2s_startup,
	.shutdown = wb_i2s_shutdown,
	.trigger = wb_i2s_trigger,
	.hw_params = wb_i2s_hw_params,
};

static struct snd_soc_dai_driver wb_i2s_dai = {
	/*.probe = wb_i2s_dai_probe,*/
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = WB_I2S_RATES,
		.formats = SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_U32_LE,
	},

	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = WB_I2S_RATES,
		.formats = SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_U32_LE,
	},

	.ops = &wb_i2s_dai_ops,
	.symmetric_rates = 1,
};

static const struct snd_soc_component_driver wb_i2s_component = {
	.name = "wb-i2s",
};

static int wb_i2s_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct wb_i2s *i2s;
	int ret;

	printk("SJK DEBUG: %s\n", __func__);

	i2s = devm_kzalloc(&pdev->dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	platform_set_drvdata(pdev, i2s);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2s->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(i2s->regs))
		return PTR_ERR(i2s->regs);

	ret = devm_snd_soc_register_component(&pdev->dev, &wb_i2s_component,
					      &wb_i2s_dai, 1);
	if (ret)
		return ret;

	if (ret = wb_pcm_dma_platform_register(&pdev->dev)) {
		printk("SJK DEBUG: wb_pcm_dma_platform_register failed\n");
		return ret;
	}

	return 0;
}

static int wb_i2s_dev_remove(struct platform_device *pdev)
{
	printk("SJK DEBUG: %s\n", __func__);

	return 0;
}

static const struct of_device_id wb_i2s_of_match[] = {
	{ .compatible = "wb-i2s", },
	{},
};
MODULE_DEVICE_TABLE(of, wb_i2s_of_match);

static struct platform_driver wb_i2s_driver = {
	.driver = {
		.name = "wb-i2s",
		.owner = THIS_MODULE,
		.of_match_table = wb_i2s_of_match,
	},
	.probe = wb_i2s_probe,
	.remove = wb_i2s_dev_remove,
};
module_platform_driver(wb_i2s_driver);

MODULE_AUTHOR("Stefan Kristiansson <stefan.kristiansson@saunalahti.fi>");
MODULE_DESCRIPTION("Wishbone I2S driver");
MODULE_LICENSE("GPL");
